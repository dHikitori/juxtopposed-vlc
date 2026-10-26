// SPDX-License-Identifier: GPL-2.0-or-later
//! Portable TheSubtitleDB protocol adapter. All IO is performed by the host.
use serde_json::{json, Value};
use std::collections::{BTreeMap, BTreeSet};

const API: &str = "https://api.thesubtitledb.org";
const MAX_INPUT: usize = 1024 * 1024;
const MAX_PENDING: usize = 8;
const MAX_ISSUED: usize = 2048;
const PAGE_SIZE: usize = 50;

#[derive(Clone)]
struct Lookup {
    invocation: String,
    identity: Value,
    language: String,
    title_lookup: bool,
    episode_followup: bool,
    expected_imdb: Option<String>,
    expected_tmdb: Option<u64>,
    offset: usize,
}

#[derive(Default)]
pub struct Provider {
    next_id: u64,
    pending: BTreeMap<String, Lookup>,
    issued: BTreeMap<String, Value>,
}

fn result(id: &str, status: &str, data: Value, error: Option<&str>) -> Value {
    let mut value = json!({"type":"result","id":id,"status":status,"data":data});
    if let Some(error) = error {
        value["error"] = Value::String(error.to_owned());
    }
    value
}

fn failure(id: &str, status: &str, message: &str) -> Vec<Value> {
    vec![result(id, status, json!({"candidates":[]}), Some(message))]
}

fn text(value: &Value, key: &str) -> String {
    value
        .get(key)
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_owned()
}

fn integer(value: &Value) -> Option<u64> {
    value
        .as_u64()
        .or_else(|| value.as_str()?.parse::<u64>().ok())
}

fn imdb_id(value: &str) -> Option<String> {
    let digits = value.strip_prefix("tt").unwrap_or(value);
    if (7..=12).contains(&digits.len()) && digits.bytes().all(|b| b.is_ascii_digit()) {
        Some(format!("tt{digits}"))
    } else {
        None
    }
}

fn scoped_id(identity: &Value, key: &str, namespace: &str, scope: &str) -> Option<String> {
    if let Some(value) = identity.get("ids").and_then(|ids| ids.get(key)) {
        if let Some(value) = value.as_str() {
            return Some(value.to_owned());
        }
        if let Some(value) = value.as_u64() {
            return Some(value.to_string());
        }
    }
    identity
        .get("externalIds")?
        .as_array()?
        .iter()
        .find_map(|value| {
            if text(value, "namespace") == namespace && text(value, "scope") == scope {
                let id = value.get("value")?;
                id.as_str()
                    .map(str::to_owned)
                    .or_else(|| id.as_u64().map(|n| n.to_string()))
            } else {
                None
            }
        })
}

fn episode(identity: &Value) -> Option<(u64, u64)> {
    let nested = identity.get("episode");
    if let Some(value) = nested {
        if value
            .get("scheme")
            .and_then(Value::as_str)
            .is_some_and(|s| s != "seasonal")
        {
            return None;
        }
    }
    let season = identity
        .get("season")
        .and_then(integer)
        .or_else(|| nested?.get("season").and_then(integer))?;
    let episodes = identity
        .get("episodes")
        .and_then(Value::as_array)
        .or_else(|| nested?.get("episodes").and_then(Value::as_array))?;
    if episodes.len() != 1 {
        return None;
    }
    let number = integer(&episodes[0])?;
    if season > 10000 || number == 0 || number > 10000 {
        return None;
    }
    Some((season, number))
}

fn encoded(value: &str) -> String {
    let mut out = String::new();
    for b in value.bytes() {
        if b.is_ascii_alphanumeric() || b"-._~".contains(&b) {
            out.push(b as char);
        } else {
            out.push_str(&format!("%{b:02X}"));
        }
    }
    out
}

fn language(value: &str) -> Option<String> {
    if !(2..=16).contains(&value.len())
        || !value
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'-')
    {
        return None;
    }
    if value.starts_with('-') || value.ends_with('-') || value.contains("--") {
        return None;
    }
    Some(value.to_owned())
}

fn canonical_language(value: &str) -> String {
    match value {
        "pb" => "pt-BR",
        "zt" => "zh-Hant",
        "ea" => "es-419",
        "sp" => "es-ES",
        "pm" => "pt-MZ",
        "zc" => "yue",
        "me" => "cnr",
        "at" => "ast",
        "ma" => "mni",
        "sb" => "wen",
        "tp" => "tok",
        _ => value,
    }
    .to_owned()
}

impl Provider {
    pub fn handle(&mut self, message: &Value) -> Vec<Value> {
        match text(message, "type").as_str() {
            "invoke" => self.invoke(message),
            "host-result" => self.complete(message),
            "cancel" => {
                let id = text(message, "id");
                self.pending.retain(|_, job| job.invocation != id);
                vec![]
            }
            _ => vec![],
        }
    }

    fn invoke(&mut self, message: &Value) -> Vec<Value> {
        let id = text(message, "id");
        if id.is_empty() || id.len() > 200 {
            return vec![];
        }
        if text(message, "contribution") != "search" {
            return failure(
                &id,
                "unsupported",
                "This package exposes only the search contribution.",
            );
        }
        if message
            .pointer("/credentials/apiKey/present")
            .and_then(Value::as_bool)
            != Some(true)
        {
            return failure(
                &id,
                "auth_required",
                "Add your TheSubtitleDB API key in Add-on settings to search.",
            );
        }
        match text(message, "operation").as_str() {
            "searchAssets" => self.search(&id, message),
            "resolveAsset" => self.resolve(&id, &message["context"]["candidate"]),
            _ => failure(
                &id,
                "unsupported",
                "The requested operation is not supported.",
            ),
        }
    }

    fn search(&mut self, id: &str, message: &Value) -> Vec<Value> {
        if self.pending.len() >= MAX_PENDING {
            return failure(id, "error", "Too many active subtitle searches.");
        }
        if self.pending.values().any(|job| job.invocation == id) {
            return failure(id, "error", "Duplicate active invocation.");
        }
        let context = &message["context"];
        let mut identity = context
            .get("identity")
            .cloned()
            .unwrap_or_else(|| json!({}));
        if identity.get("kind").is_none() && identity.is_object() {
            identity["kind"] = json!(text(&identity, "type"));
        }
        let kind = text(&identity, "kind");
        if kind != "movie" && kind != "episode" {
            return failure(
                id,
                "unsupported",
                "Select a movie or a numbered episode first.",
            );
        }
        if kind == "episode" && episode(&identity).is_none() {
            return failure(
                id,
                "unsupported",
                "A single episode with a season and episode number is required.",
            );
        }
        let requested_language = context
            .get("language")
            .and_then(Value::as_str)
            .or_else(|| message["settings"].get("language").and_then(Value::as_str))
            .unwrap_or("en");
        let Some(language) = language(requested_language) else {
            return failure(id, "error", "The subtitle language is invalid.");
        };
        let offset = context.get("cursor").and_then(integer).unwrap_or(0) as usize;
        if offset > 10000 {
            return failure(
                id,
                "unsupported",
                "The subtitle page is outside the supported range.",
            );
        }
        let mut job = Lookup {
            invocation: id.to_owned(),
            identity,
            language,
            title_lookup: false,
            episode_followup: false,
            expected_imdb: None,
            expected_tmdb: None,
            offset,
        };
        let (imdb_key, tmdb_key, scope) = if kind == "movie" {
            ("imdb", "tmdbMovie", "movie")
        } else {
            ("seriesImdb", "seriesTmdb", "series")
        };
        let manual_query = context.get("manual").and_then(Value::as_bool) == Some(true)
            && context
                .get("query")
                .and_then(Value::as_str)
                .is_some_and(|query| !query.trim().is_empty());
        let imdb = if manual_query {
            None
        } else {
            scoped_id(&job.identity, imdb_key, "imdb", scope).and_then(|v| imdb_id(&v))
        };
        let tmdb = if manual_query {
            None
        } else {
            scoped_id(&job.identity, tmdb_key, "tmdb", scope)
                .and_then(|v| v.parse::<u64>().ok())
                .filter(|n| *n > 0)
        };
        let mut path;
        if let Some(imdb) = imdb {
            path = format!("/v1/by-imdb/{imdb}");
            job.expected_imdb = Some(imdb);
        } else if let Some(tmdb) = tmdb {
            path = format!(
                "/v1/by-tmdb/{}/{tmdb}",
                if kind == "movie" { "movie" } else { "tv" }
            );
            job.expected_tmdb = Some(tmdb);
        } else {
            let query = context
                .get("query")
                .and_then(Value::as_str)
                .filter(|v| !v.trim().is_empty())
                .or_else(|| job.identity.get("title").and_then(Value::as_str))
                .or_else(|| {
                    job.identity
                        .get("titles")
                        .and_then(Value::as_array)?
                        .first()?
                        .as_str()
                })
                .unwrap_or("")
                .trim();
            if query.is_empty() || query.len() > 512 {
                return failure(
                    id,
                    "unsupported",
                    "Supply a title or a scoped IMDb/TMDB identifier.",
                );
            }
            path = format!(
                "/v1/by-title?q={}&type={}",
                encoded(query),
                if kind == "movie" { "movie" } else { "tv" }
            );
            job.title_lookup = true;
            job.episode_followup = kind == "episode";
        }
        if kind == "episode" && !job.title_lookup {
            let (season, number) = episode(&job.identity).unwrap();
            path.push_str(&format!("/season/{season}/episode/{number}"));
        }
        let separator = if path.contains('?') { '&' } else { '?' };
        let limit = if job.episode_followup { 1 } else { PAGE_SIZE };
        path.push_str(&format!(
            "{separator}lang={}&limit={limit}&offset={offset}&response_class=minimal",
            encoded(&job.language)
        ));
        self.request(job, format!("{API}{path}"))
    }

    fn request(&mut self, job: Lookup, url: String) -> Vec<Value> {
        self.next_id += 1;
        let call_id = format!("tsdb-{}", self.next_id);
        let call = json!({"type":"host-call","id":call_id,"invocation":job.invocation,"operation":"network.http",
            "request":{"url":url,"method":"GET","credential":"apiKey","headers":{"Accept":"application/json"}}});
        self.pending.insert(call_id, job);
        vec![call]
    }

    fn complete(&mut self, message: &Value) -> Vec<Value> {
        let call_id = text(message, "id");
        let Some(job) = self.pending.get(&call_id) else {
            return vec![];
        };
        if text(message, "invocation") != job.invocation {
            return vec![];
        }
        let mut job = self.pending.remove(&call_id).unwrap();
        if text(message, "status") != "ok" {
            return failure(
                &job.invocation,
                "error",
                "The subtitle service could not be reached.",
            );
        }
        let response = &message["response"];
        let status = response.get("status").and_then(Value::as_u64).unwrap_or(0);
        match status {
            401 | 403 => {
                return failure(
                    &job.invocation,
                    "auth_required",
                    "TheSubtitleDB refused this API key. Update it in Add-on settings.",
                )
            }
            404 => {
                return vec![result(
                    &job.invocation,
                    "no_match",
                    json!({"candidates":[]}),
                    None,
                )]
            }
            429 | 503 => {
                let retry = response["headers"]
                    .get("retry-after")
                    .or_else(|| response["headers"].get("Retry-After"))
                    .and_then(|v| integer(v))
                    .unwrap_or(60)
                    .clamp(1, 86400);
                return vec![result(
                    &job.invocation,
                    "rate_limited",
                    json!({"candidates":[],"retryAfterSeconds":retry}),
                    Some("The subtitle service is busy. Try again later."),
                )];
            }
            200 => (),
            _ => {
                return failure(
                    &job.invocation,
                    "error",
                    "The subtitle service returned an unexpected status.",
                )
            }
        }
        let body = response.get("body").and_then(Value::as_str).unwrap_or("");
        if body.len() > MAX_INPUT {
            return failure(
                &job.invocation,
                "error",
                "The subtitle response exceeds the allowed size.",
            );
        }
        let Ok(document) = serde_json::from_str::<Value>(body) else {
            return failure(
                &job.invocation,
                "error",
                "The subtitle service returned invalid JSON.",
            );
        };
        let title = &document["title"];
        let returned_imdb = imdb_id(&text(title, "imdb"));
        let returned_tmdb = title.get("tmdb_id").and_then(integer);
        let kind = text(&job.identity, "kind");
        let expected_type = if kind == "movie" { "movie" } else { "tv" };
        if text(title, "media_type") != expected_type
            || returned_imdb.is_none()
            || job
                .expected_imdb
                .as_ref()
                .is_some_and(|id| returned_imdb.as_ref() != Some(id))
            || job
                .expected_tmdb
                .is_some_and(|id| returned_tmdb != Some(id))
        {
            return failure(
                &job.invocation,
                "error",
                "The subtitle response identifies a different movie or series.",
            );
        }
        if job.episode_followup {
            let (season, number) = episode(&job.identity).unwrap();
            job.expected_imdb = returned_imdb.clone();
            job.episode_followup = false;
            let url = format!("{API}/v1/by-imdb/{}/season/{season}/episode/{number}?lang={}&limit={PAGE_SIZE}&offset={}&response_class=minimal",
                returned_imdb.unwrap(), encoded(&job.language), job.offset);
            return self.request(job, url);
        }
        let Some(items) = document
            .pointer("/subtitles/items")
            .and_then(Value::as_array)
        else {
            return failure(
                &job.invocation,
                "error",
                "The subtitle service returned an invalid candidate list.",
            );
        };
        if items.len() > 100 {
            return failure(
                &job.invocation,
                "error",
                "The subtitle response contains too many candidates.",
            );
        }
        let mut identity = job.identity.clone();
        if !identity.is_object() {
            identity = json!({});
        }
        if job.title_lookup {
            identity["ids"] = json!({});
            identity.as_object_mut().unwrap().remove("externalIds");
        }
        if !identity.get("ids").is_some_and(Value::is_object) {
            identity["ids"] = json!({});
        }
        let imdb_key = if kind == "movie" {
            "imdb"
        } else {
            "seriesImdb"
        };
        identity["ids"][imdb_key] = json!(returned_imdb.unwrap());
        if let Some(tmdb) = returned_tmdb {
            identity["ids"][if kind == "movie" {
                "tmdbMovie"
            } else {
                "seriesTmdb"
            }] = json!(tmdb.to_string());
        }
        identity["title"] = json!(text(title, "name"));
        if let Some(year) = title.get("year").and_then(integer) {
            identity["year"] = json!(year);
        }
        if let Some((season, number)) = episode(&identity).filter(|_| kind == "episode") {
            identity["season"] = json!(season);
            identity["episodes"] = json!([number]);
        }
        let mut candidates = Vec::new();
        let mut seen = BTreeSet::new();
        for item in items {
            let Some(id) = item.get("id").and_then(integer).filter(|n| *n > 0) else {
                continue;
            };
            let id = id.to_string();
            if !seen.insert(id.clone()) {
                continue;
            }
            let language = canonical_language(&text(item, "language"));
            if language.is_empty() {
                continue;
            }
            let format = text(item, "format").to_ascii_lowercase();
            if ![
                "srt", "vtt", "ass", "ssa", "sub", "txt", "smi", "sami", "mpl", "tmp", "usf",
            ]
            .contains(&format.as_str())
            {
                continue;
            }
            let name = text(item, "release_name");
            let name = if name.is_empty() {
                format!("{} ({language})", text(title, "name"))
            } else {
                name.chars().take(512).collect()
            };
            let mut candidate = json!({"id":id,"name":name,"language":language,"format":format,"identity":identity,
                "evidence":{"source":if job.title_lookup {"title-lookup"} else {"scoped-id"},"hashMatched":false},
                "releaseName":text(item,"release_name").chars().take(512).collect::<String>(),
                "hearingImpaired":item.get("hearing_impaired").and_then(Value::as_bool).unwrap_or(false),
                "aiTranscribed":item.get("ai_transcribed").and_then(Value::as_bool).unwrap_or(false),
                "provider":"org.videolan.thesubtitledb/search",
                "attribution":{"name":"TheSubtitleDB","url":"https://thesubtitledb.org"}});
            if let Some(fps) = item
                .get("fps")
                .and_then(Value::as_f64)
                .filter(|n| n.is_finite() && *n > 0.0 && *n < 1000.0)
            {
                candidate["fps"] = json!(fps);
            }
            if let Some(bytes) = item.get("bytes").and_then(integer) {
                candidate["bytes"] = json!(bytes);
            }
            if self.issued.len() >= MAX_ISSUED {
                self.issued.clear();
            }
            self.issued.insert(id, candidate.clone());
            candidates.push(candidate);
        }
        let total = document
            .pointer("/subtitles/total")
            .and_then(integer)
            .unwrap_or(items.len() as u64);
        let next = job.offset + items.len();
        let mut data = json!({"candidates":candidates,"total":total});
        if next < total as usize && next <= 10000 && !items.is_empty() {
            data["cursor"] = json!(next.to_string());
        }
        vec![result(
            &job.invocation,
            if data["candidates"].as_array().unwrap().is_empty() {
                "no_match"
            } else {
                "match"
            },
            data,
            None,
        )]
    }

    fn resolve(&self, invocation: &str, candidate: &Value) -> Vec<Value> {
        let id = text(candidate, "id");
        let Some(issued) = self.issued.get(&id) else {
            return failure(
                invocation,
                "error",
                "Search again before downloading this subtitle.",
            );
        };
        if candidate
            .get("identity")
            .is_some_and(|identity| identity != &issued["identity"])
        {
            return failure(
                invocation,
                "error",
                "The subtitle target changed. Search again.",
            );
        }
        let format = text(issued, "format");
        vec![result(
            invocation,
            "match",
            json!({"url":format!("{API}/get/{id}"),"fileName":format!("thesubtitledb-{id}.{format}"),
            "format":format,"credential":"apiKey"}),
            None,
        )]
    }
}

#[cfg(target_arch = "wasm32")]
#[link(wasm_import_module = "vlc")]
extern "C" {
    fn host_send(ptr: i32, len: i32) -> i32;
}

#[cfg(target_arch = "wasm32")]
std::thread_local! {
    static PROVIDER: std::cell::RefCell<Provider> = std::cell::RefCell::new(Provider::default());
}

/// Allocate a host-owned dispatch buffer. The host must pass this exact length to dealloc.
#[no_mangle]
pub extern "C" fn vlc_alloc(len: i32) -> i32 {
    if len <= 0 || len as usize > MAX_INPUT {
        return 0;
    }
    let buffer = vec![0_u8; len as usize].into_boxed_slice();
    Box::into_raw(buffer) as *mut u8 as usize as i32
}

/// Release a buffer previously returned by vlc_alloc after dispatch has returned.
#[no_mangle]
pub unsafe extern "C" fn vlc_dealloc(ptr: i32, len: i32) {
    if ptr == 0 || len <= 0 || len as usize > MAX_INPUT {
        return;
    }
    drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
        ptr as u32 as usize as *mut u8,
        len as usize,
    )));
}

#[cfg(target_arch = "wasm32")]
#[no_mangle]
pub unsafe extern "C" fn vlc_dispatch(ptr: i32, len: i32) {
    if ptr == 0 || len <= 0 || len as usize > MAX_INPUT {
        return;
    }
    let input = std::slice::from_raw_parts(ptr as u32 as usize as *const u8, len as usize);
    let Ok(message) = serde_json::from_slice::<Value>(input) else {
        return;
    };
    let replies = PROVIDER.with(|provider| provider.borrow_mut().handle(&message));
    for reply in replies {
        if let Ok(bytes) = serde_json::to_vec(&reply) {
            if bytes.len() <= MAX_INPUT
                && host_send(bytes.as_ptr() as usize as i32, bytes.len() as i32) != 0
            {
                break;
            }
        }
    }
}

#[cfg(test)]
mod tests;

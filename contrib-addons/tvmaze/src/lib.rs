// SPDX-License-Identifier: GPL-2.0-or-later
//! Portable TVmaze episode metadata. The host owns each HTTP request.
use serde_json::{json, Value};
use std::collections::BTreeMap;

const API: &str = "https://api.tvmaze.com";
const MAX_INPUT: usize = 1024 * 1024;
const MAX_BODY: usize = 512 * 1024;
const MAX_PENDING: usize = 8;
const MAX_CANDIDATES: usize = 8;

#[derive(Clone)]
enum Stage {
    Identify,
    Show(bool),
    Seasons,
    Episodes,
}
#[derive(Clone)]
struct Lookup {
    invocation: String,
    identity: Value,
    season: u64,
    episode: u64,
    stage: Stage,
    show: Value,
    path: String,
    cache_generation: u64,
}
#[derive(Default)]
pub struct Provider {
    sequence: u64,
    pending: BTreeMap<String, Lookup>,
    series_key: String,
    cache_generation: u64,
    cache: BTreeMap<String, String>,
}
fn text(value: &Value, key: &str) -> String {
    value
        .get(key)
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_owned()
}
fn number(value: &Value) -> Option<u64> {
    value.as_u64().or_else(|| value.as_str()?.parse().ok())
}
fn normalized(value: &str) -> String {
    value
        .chars()
        .filter(|c| c.is_alphanumeric())
        .flat_map(char::to_lowercase)
        .collect()
}
fn encode(value: &str) -> String {
    let mut result = String::new();
    for byte in value.bytes() {
        if byte.is_ascii_alphanumeric() || b"-._~".contains(&byte) {
            result.push(byte as char);
        } else {
            result.push_str(&format!("%{byte:02X}"));
        }
    }
    result
}
fn scoped_id(identity: &Value, key: &str, namespace: &str) -> Option<String> {
    if let Some(value) = identity.get("ids").and_then(|ids| ids.get(key)) {
        return value
            .as_str()
            .map(str::to_owned)
            .or_else(|| value.as_u64().map(|n| n.to_string()));
    }
    identity
        .get("externalIds")?
        .as_array()?
        .iter()
        .find_map(|id| {
            if text(id, "namespace") != namespace || text(id, "scope") != "series" {
                return None;
            }
            id.get("value").and_then(|v| {
                v.as_str()
                    .map(str::to_owned)
                    .or_else(|| v.as_u64().map(|n| n.to_string()))
            })
        })
}
fn seasonal(identity: &Value) -> Option<(u64, u64)> {
    let nested = identity.get("episode");
    if nested.is_some_and(|v| {
        v.get("scheme")
            .and_then(Value::as_str)
            .is_some_and(|s| s != "seasonal")
    }) {
        return None;
    }
    let season = identity
        .get("season")
        .and_then(number)
        .or_else(|| nested?.get("season").and_then(number))?;
    let episodes = identity
        .get("episodes")
        .and_then(Value::as_array)
        .or_else(|| nested?.get("episodes").and_then(Value::as_array))?;
    if episodes.len() != 1 {
        return None;
    }
    let episode = number(&episodes[0])?;
    if season == 0 || season > 999 || episode == 0 || episode > 9999 {
        return None;
    }
    Some((season, episode))
}
// The episode identity a show gives, in the host's identity keys.
fn episode_identity(show: &Value, season: u64, episode: u64) -> Value {
    let mut identity = json!({"kind":"episode","title":text(show, "name"),"season":season,"episodes":[episode],
        "ids":{"tvmazeShow":number(&show["id"]).unwrap_or(0).to_string()}});
    if let Some(year) = text(show, "premiered")
        .get(..4)
        .and_then(|value| value.parse::<u64>().ok())
    {
        identity["year"] = json!(year);
    }
    if let Some(imdb) = show["externals"]["imdb"].as_str() {
        identity["ids"]["seriesImdb"] = json!(imdb);
    }
    if let Some(tvdb) = number(&show["externals"]["thetvdb"]) {
        identity["ids"]["seriesTvdb"] = json!(tvdb.to_string());
    }
    identity
}
// The offline install check: run the show search and episode parsing on a
// built-in sample in a separate provider, with no host call.
fn self_test(id: &str) -> Vec<Value> {
    if id.is_empty() || id.len() > 256 {
        return Vec::new();
    }
    let mut provider = Provider::default();
    let request = json!({"type":"invoke","id":"self-test","contribution":"episodes","operation":"identify",
        "context":{"identity":{"kind":"episode","title":"Sample Series","season":1,"episodes":[2]},"fields":[]}});
    let passed = provider
        .handle(&request)
        .first()
        .filter(|call| call["type"] == "host-call")
        .map(|call| {
            let body = json!([{"show":{"id":7,"name":"Sample Series","premiered":"2001-01-01"}}]).to_string();
            provider.complete(&json!({"type":"host-result","id":call["id"],"invocation":call["invocation"],
                "status":"ok","response":{"status":200,"body":body,"headers":{}}}))
        })
        .and_then(|replies| replies.into_iter().next())
        .is_some_and(|reply| {
            reply["status"] == "match"
                && reply["data"]["candidates"][0]["identity"]["ids"]["tvmazeShow"] == "7"
        });
    reply(id, if passed { "ok" } else { "error" }, json!({}), "")
}
// TVmaze data is CC BY-SA and its API answers change slowly: the host may
// reuse an answer for an hour, save accepted titles to the library and show
// an expired answer for a day while TVmaze cannot be reached.
fn retention() -> Value {
    json!({"reuseSeconds":3600,"keep":"library","offlineSeconds":86400})
}

fn reply(id: &str, status: &str, data: Value, error: &str) -> Vec<Value> {
    let mut result = json!({"type":"result","id":id,"status":status,"data":data});
    if !error.is_empty() {
        result["error"] = json!(error);
    }
    vec![result]
}
fn failed(id: &str, status: &str, error: &str) -> Vec<Value> {
    reply(id, status, json!({}), error)
}
fn show_matches(show: &Value, identity: &Value, require_title: bool) -> bool {
    if number(&show["id"]).is_none_or(|id| id == 0) {
        return false;
    }
    let title = text(identity, "title");
    if require_title && (title.is_empty() || normalized(&text(show, "name")) != normalized(&title))
    {
        return false;
    }
    let year = identity.get("year").and_then(number).unwrap_or(0);
    let premiere = text(show, "premiered");
    if year > 0 && premiere.get(..4).and_then(|v| v.parse::<u64>().ok()) != Some(year) {
        return false;
    }
    if let Some(imdb) = scoped_id(identity, "seriesImdb", "imdb") {
        if text(&show["externals"], "imdb") != imdb {
            return false;
        }
    }
    if let Some(tvdb) = scoped_id(identity, "seriesTvdb", "tvdb") {
        if number(&show["externals"]["thetvdb"]).map(|id| id.to_string()) != Some(tvdb) {
            return false;
        }
    }
    if let Some(tvmaze) = scoped_id(identity, "tvmazeShow", "tvmaze") {
        if number(&show["id"]).map(|id| id.to_string()) != Some(tvmaze) {
            return false;
        }
    }
    true
}

impl Provider {
    fn request(&mut self, mut lookup: Lookup, path: String) -> Vec<Value> {
        self.sequence += 1;
        let id = format!("tvmaze-http-{}", self.sequence);
        let message = json!({"type":"host-call","id":id,"invocation":lookup.invocation,
            "operation":"network.http","request":{"url":format!("{API}{path}"),"method":"GET",
                "headers":{"Accept":"application/json"}}});
        let cached = if lookup.cache_generation == self.cache_generation {
            self.cache.get(&path).cloned()
        } else {
            None
        };
        lookup.path = path;
        let invocation = lookup.invocation.clone();
        self.pending.insert(id.clone(), lookup);
        if let Some(body) = cached {
            return self.complete(
                &json!({"type":"host-result","id":id,"invocation":invocation,"status":"ok",
                "response":{"status":200,"body":body,"headers":{}}}),
            );
        }
        vec![message]
    }
    fn remember(&mut self, lookup: &Lookup, body: &str) {
        if lookup.cache_generation != self.cache_generation {
            return;
        }
        if self.cache.len() >= 8 && !self.cache.contains_key(&lookup.path) {
            if let Some(first) = self.cache.keys().next().cloned() {
                self.cache.remove(&first);
            }
        }
        self.cache.insert(lookup.path.clone(), body.to_owned());
    }
    pub fn handle(&mut self, message: &Value) -> Vec<Value> {
        match text(message, "type").as_str() {
            "invoke" => self.invoke(message),
            "host-result" => self.complete(message),
            "self-test" => self_test(&text(message, "id")),
            "cancel" => {
                let id = text(message, "id");
                self.pending.retain(|_, lookup| lookup.invocation != id);
                Vec::new()
            }
            _ => Vec::new(),
        }
    }
    fn invoke(&mut self, message: &Value) -> Vec<Value> {
        let id = text(message, "id");
        if id.is_empty() || id.len() > 256 {
            return Vec::new();
        }
        let operation = text(message, "operation");
        let identify = operation == "identify";
        if text(message, "contribution") != "episodes" || (operation != "enrich" && !identify) {
            return failed(
                &id,
                "unsupported",
                "This provider supplies episode titles and series choices only.",
            );
        }
        if self.pending.len() >= MAX_PENDING
            || self.pending.values().any(|lookup| lookup.invocation == id)
        {
            return failed(
                &id,
                "rate_limited",
                "The provider has too many active requests.",
            );
        }
        let context = &message["context"];
        let mut identity = context["identity"].clone();
        let kind = identity
            .get("kind")
            .and_then(Value::as_str)
            .or_else(|| identity.get("type").and_then(Value::as_str))
            .unwrap_or("");
        if kind != "episode"
            || (!identify
                && !context["fields"]
                    .as_array()
                    .is_some_and(|fields| fields.iter().any(|f| f == "episodeTitle")))
        {
            return failed(
                &id,
                "unsupported",
                "This provider requires a seasonal episode and the episodeTitle field.",
            );
        }
        let Some((season, episode)) = seasonal(&identity) else {
            return failed(
                &id,
                "unsupported",
                "This provider requires one numbered seasonal episode.",
            );
        };
        let query = text(context, "query");
        if context["manual"].as_bool() == Some(true) && !query.trim().is_empty() {
            identity["title"] = json!(query.trim());
            identity["ids"] = json!({});
            identity["externalIds"] = json!([]);
            identity["year"] = json!(0);
        }
        let series_key = json!({"title":identity["title"],"year":identity["year"],"ids":identity["ids"],"externalIds":identity["externalIds"]}).to_string();
        if series_key != self.series_key {
            self.series_key = series_key;
            self.cache_generation += 1;
            self.cache.clear();
        }
        let mut lookup = Lookup {
            invocation: id,
            identity,
            season,
            episode,
            stage: Stage::Show(false),
            show: Value::Null,
            path: String::new(),
            cache_generation: self.cache_generation,
        };
        if identify {
            // Offer each show the search returns; the user chooses one.
            let title = text(&lookup.identity, "title");
            if title.trim().is_empty() || title.len() > 1024 {
                return failed(
                    &lookup.invocation,
                    "no_match",
                    "The series title is not available.",
                );
            }
            lookup.stage = Stage::Identify;
            return self.request(lookup, format!("/search/shows?q={}", encode(&title)));
        }
        let path;
        if let Some(tvmaze) = scoped_id(&lookup.identity, "tvmazeShow", "tvmaze")
            .and_then(|id| id.parse::<u64>().ok())
            .filter(|id| *id > 0)
        {
            path = format!("/shows/{tvmaze}");
        } else if let Some(imdb) = scoped_id(&lookup.identity, "seriesImdb", "imdb").filter(|id| {
            id.starts_with("tt")
                && (9..=14).contains(&id.len())
                && id[2..].bytes().all(|b| b.is_ascii_digit())
        }) {
            path = format!("/lookup/shows?imdb={imdb}");
        } else if let Some(tvdb) = scoped_id(&lookup.identity, "seriesTvdb", "tvdb")
            .and_then(|id| id.parse::<u64>().ok())
            .filter(|id| *id > 0)
        {
            path = format!("/lookup/shows?thetvdb={tvdb}");
        } else {
            let title = text(&lookup.identity, "title");
            if title.trim().is_empty() || title.len() > 1024 {
                return failed(
                    &lookup.invocation,
                    "no_match",
                    "The series title is not available.",
                );
            }
            lookup.stage = Stage::Show(true);
            path = format!("/search/shows?q={}", encode(&title));
        }
        self.request(lookup, path)
    }
    fn complete(&mut self, message: &Value) -> Vec<Value> {
        let key = text(message, "id");
        let Some(current) = self.pending.get(&key) else {
            return Vec::new();
        };
        if text(message, "invocation") != current.invocation {
            return Vec::new();
        }
        let mut lookup = self.pending.remove(&key).unwrap();
        if text(message, "status") != "ok" {
            return failed(&lookup.invocation, "error", "The TVmaze request failed.");
        }
        let response = &message["response"];
        match number(&response["status"]).unwrap_or(0) {
            200 => (),
            404 => return reply(&lookup.invocation, "no_match", json!({}), ""),
            401 | 403 => {
                return failed(
                    &lookup.invocation,
                    "auth_required",
                    "TVmaze did not permit this request.",
                )
            }
            429 => {
                let seconds = response["headers"]
                    .as_object()
                    .and_then(|headers| {
                        headers
                            .iter()
                            .find(|(key, _)| key.eq_ignore_ascii_case("retry-after"))
                    })
                    .and_then(|(_, value)| number(value))
                    .unwrap_or(10)
                    .clamp(1, 3600);
                return reply(
                    &lookup.invocation,
                    "rate_limited",
                    json!({"retryAfterSeconds":seconds}),
                    "TVmaze requires a delay before the next request.",
                );
            }
            _ => {
                return failed(
                    &lookup.invocation,
                    "error",
                    "TVmaze returned an HTTP error.",
                )
            }
        }
        let body = text(response, "body");
        if body.len() > MAX_BODY {
            return failed(
                &lookup.invocation,
                "error",
                "The TVmaze response exceeds the size limit.",
            );
        }
        let Ok(value) = serde_json::from_str::<Value>(&body) else {
            return failed(
                &lookup.invocation,
                "error",
                "The TVmaze response is not valid JSON.",
            );
        };
        match lookup.stage {
            Stage::Identify => {
                let Some(entries) = value.as_array().filter(|items| items.len() <= 100) else {
                    return failed(
                        &lookup.invocation,
                        "error",
                        "The TVmaze show list is invalid.",
                    );
                };
                let candidates: Vec<Value> = entries
                    .iter()
                    .map(|entry| &entry["show"])
                    .filter(|show| show_matches(show, &lookup.identity, false))
                    .filter(|show| {
                        let name = text(show, "name");
                        !name.trim().is_empty() && name.len() <= 1024
                    })
                    .take(MAX_CANDIDATES)
                    .map(|show| {
                        let source = text(show, "url");
                        let mut candidate = json!({"identity":episode_identity(show, lookup.season, lookup.episode)});
                        if source.starts_with("https://www.tvmaze.com/shows/") && source.len() <= 2048 {
                            candidate["attribution"] =
                                json!({"name":"TVmaze","url":source,"license":"CC-BY-SA-4.0"});
                        }
                        candidate
                    })
                    .collect();
                if candidates.is_empty() {
                    return reply(&lookup.invocation, "no_match", json!({}), "");
                }
                self.remember(&lookup, &body);
                reply(
                    &lookup.invocation,
                    "match",
                    json!({"candidates":candidates,"retention":retention()}),
                    "",
                )
            }
            Stage::Show(search) => {
                let shows: Vec<Value> = if search {
                    let Some(entries) = value.as_array().filter(|items| items.len() <= 100) else {
                        return failed(
                            &lookup.invocation,
                            "error",
                            "The TVmaze show list is invalid.",
                        );
                    };
                    entries
                        .iter()
                        .map(|entry| entry["show"].clone())
                        .filter(|show| show_matches(show, &lookup.identity, true))
                        .collect()
                } else if show_matches(&value, &lookup.identity, false) {
                    vec![value.clone()]
                } else {
                    Vec::new()
                };
                if shows.len() > 1 {
                    return reply(
                        &lookup.invocation,
                        "ambiguous",
                        json!({}),
                        "More than one series has this title. Select a series ID.",
                    );
                }
                let Some(show) = shows.into_iter().next() else {
                    return reply(&lookup.invocation, "no_match", json!({}), "");
                };
                let id = number(&show["id"]).unwrap();
                self.remember(&lookup, &body);
                lookup.show = show;
                lookup.stage = Stage::Seasons;
                self.request(lookup, format!("/shows/{id}/seasons"))
            }
            Stage::Seasons => {
                let Some(seasons) = value.as_array().filter(|items| items.len() <= 1000) else {
                    return failed(
                        &lookup.invocation,
                        "error",
                        "The TVmaze season list is invalid.",
                    );
                };
                let matches: Vec<_> = seasons
                    .iter()
                    .filter(|entry| number(&entry["number"]) == Some(lookup.season))
                    .collect();
                if matches.len() > 1 {
                    return reply(
                        &lookup.invocation,
                        "ambiguous",
                        json!({}),
                        "More than one season has this number.",
                    );
                }
                let Some(id) = matches
                    .first()
                    .and_then(|entry| number(&entry["id"]))
                    .filter(|id| *id > 0)
                else {
                    return reply(&lookup.invocation, "no_match", json!({}), "");
                };
                self.remember(&lookup, &body);
                lookup.stage = Stage::Episodes;
                self.request(lookup, format!("/seasons/{id}/episodes"))
            }
            Stage::Episodes => {
                let Some(episodes) = value.as_array().filter(|items| items.len() <= 2000) else {
                    return failed(
                        &lookup.invocation,
                        "error",
                        "The TVmaze episode list is invalid.",
                    );
                };
                let matches: Vec<_> = episodes
                    .iter()
                    .filter(|episode| {
                        number(&episode["season"]) == Some(lookup.season)
                            && number(&episode["number"]) == Some(lookup.episode)
                    })
                    .collect();
                if matches.len() > 1 {
                    return reply(
                        &lookup.invocation,
                        "ambiguous",
                        json!({}),
                        "More than one episode has this number.",
                    );
                }
                let Some(episode) = matches.first() else {
                    return reply(&lookup.invocation, "no_match", json!({}), "");
                };
                let name = text(episode, "name");
                let source = text(episode, "url");
                if name.trim().is_empty()
                    || name.len() > 1024
                    || !source.starts_with("https://www.tvmaze.com/episodes/")
                    || source.len() > 2048
                {
                    return reply(&lookup.invocation, "no_match", json!({}), "");
                }
                self.remember(&lookup, &body);
                let identity = episode_identity(&lookup.show, lookup.season, lookup.episode);
                reply(
                    &lookup.invocation,
                    "match",
                    json!({"identity":identity,"fields":{"episodeTitle":name},
                    "attribution":{"name":"TVmaze","url":source,"license":"CC-BY-SA-4.0"},
                    "retention":retention()}),
                    "",
                )
            }
        }
    }
}

#[cfg(target_arch = "wasm32")]
#[link(wasm_import_module = "hakone")]
extern "C" {
    fn host_send(ptr: i32, len: i32) -> i32;
}
#[cfg(target_arch = "wasm32")]
std::thread_local! { static PROVIDER: std::cell::RefCell<Provider> = std::cell::RefCell::new(Provider::default()); }
#[no_mangle]
pub extern "C" fn hakone_alloc(len: i32) -> i32 {
    if len <= 0 || len as usize > MAX_INPUT {
        return 0;
    }
    Box::into_raw(vec![0_u8; len as usize].into_boxed_slice()) as *mut u8 as usize as i32
}
#[no_mangle]
pub unsafe extern "C" fn hakone_dealloc(ptr: i32, len: i32) {
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
pub unsafe extern "C" fn hakone_dispatch(ptr: i32, len: i32) {
    if ptr == 0 || len <= 0 || len as usize > MAX_INPUT {
        return;
    }
    let input = std::slice::from_raw_parts(ptr as u32 as usize as *const u8, len as usize);
    let Ok(message) = serde_json::from_slice::<Value>(input) else {
        return;
    };
    for reply in PROVIDER.with(|provider| provider.borrow_mut().handle(&message)) {
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

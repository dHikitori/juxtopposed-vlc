// SPDX-License-Identifier: GPL-2.0-or-later
//! Radio Browser discovery. The host owns network access and playback.
use hakone_addon_message_v1::{
    serde_json::{json, Map, Value},
    Addon, Incoming, Pending,
};
use std::collections::BTreeSet;

pub const MIRRORS: [&str; 1] = ["https://de1.api.radio-browser.info"];
const MAX_ATTEMPTS: usize = 2;
const PAGE: usize = 10;
const MAX_OFFSET: usize = 990;
const MAX_BODY: usize = 24 * 1024;
const MAX_ACTIVE: usize = 16;

#[derive(Clone)]
enum Kind {
    Page { key: String, offset: usize },
    Resolve { station: String },
}
struct Lookup {
    invocation: String,
    path: String,
    kind: Kind,
    mirror: usize,
    attempts: usize,
}
pub struct RadioBrowser {
    pending: Pending<Lookup>,
    active: BTreeSet<String>,
    sequence: u64,
    next_mirror: usize,
}
impl Default for RadioBrowser {
    fn default() -> Self {
        Self {
            pending: Pending::new(MAX_ACTIVE),
            active: BTreeSet::new(),
            sequence: 0,
            next_mirror: 0,
        }
    }
}
fn result(id: &str, status: &str, data: Value, error: &str) -> Value {
    let mut value = json!({"type":"result","id":id,"status":status,"data":data});
    if !error.is_empty() {
        value["error"] = json!(error);
    }
    value
}
fn encode(value: &str) -> String {
    let mut output = String::new();
    for byte in value.bytes() {
        if byte.is_ascii_alphanumeric() || b"-._~".contains(&byte) {
            output.push(byte as char);
        } else {
            output.push_str(&format!("%{byte:02X}"));
        }
    }
    output
}
fn uuid(value: &str) -> Option<String> {
    if value.len() != 36 {
        return None;
    }
    for (i, byte) in value.bytes().enumerate() {
        if [8, 13, 18, 23].contains(&i) {
            if byte != b'-' {
                return None;
            }
        } else if !byte.is_ascii_hexdigit() {
            return None;
        }
    }
    let value = value.to_ascii_lowercase();
    (value != "00000000-0000-0000-0000-000000000000").then_some(value)
}
/// This syntax filter has no DNS access. The native host checks every address.
fn public_https(value: &str) -> bool {
    if value.len() > 8192
        || !value.is_ascii()
        || value.bytes().any(|b| b <= 32 || b == 127)
        || value.contains(['\\', '#'])
    {
        return false;
    }
    let Some(rest) = value.strip_prefix("https://") else {
        return false;
    };
    let authority = rest.split(['/', '?']).next().unwrap_or("");
    let host = authority.strip_suffix(":443").unwrap_or(authority);
    if host.len() > 253 || host.contains([':', '@', '%']) {
        return false;
    }
    let labels: Vec<_> = host.split('.').collect();
    if labels.len() < 2
        || labels.iter().any(|label| {
            label.is_empty()
                || label.len() > 63
                || label.starts_with('-')
                || label.ends_with('-')
                || !label
                    .bytes()
                    .all(|b| b.is_ascii_alphanumeric() || b == b'-')
        })
    {
        return false;
    }
    if labels
        .last()
        .is_none_or(|label| label.len() < 2 || !label.bytes().all(|b| b.is_ascii_alphabetic()))
    {
        return false;
    }
    let host = host.to_ascii_lowercase();
    if [
        "localhost",
        "local",
        "internal",
        "invalid",
        "test",
        "example",
        "home.arpa",
    ]
    .iter()
    .any(|suffix| host == *suffix || host.ends_with(&format!(".{suffix}")))
    {
        return false;
    }
    let bytes = value.as_bytes();
    for i in 0..bytes.len() {
        if bytes[i] == b'%'
            && (i + 2 >= bytes.len()
                || !bytes[i + 1].is_ascii_hexdigit()
                || !bytes[i + 2].is_ascii_hexdigit())
        {
            return false;
        }
    }
    true
}
struct Station {
    id: String,
    title: String,
    url: String,
    format: &'static str,
}
fn station(value: &Value) -> Option<Station> {
    let id = uuid(value.get("stationuuid")?.as_str()?)?;
    let title = value.get("name")?.as_str()?.trim();
    if title.is_empty()
        || title.encode_utf16().count() > 512
        || title.chars().any(char::is_control)
        || title.contains("://")
    {
        return None;
    }
    if value.get("lastcheckok").and_then(Value::as_u64) != Some(1)
        || value.get("ssl_error").and_then(Value::as_u64) != Some(0)
    {
        return None;
    }
    let codec = value.get("codec")?.as_str()?.trim().to_ascii_uppercase();
    if ![
        "MP3", "AAC", "AAC+", "AACP", "OGG", "VORBIS", "OPUS", "FLAC",
    ]
    .contains(&codec.as_str())
    {
        return None;
    }
    let hls = value.get("hls")?.as_u64()?;
    if hls > 1 {
        return None;
    }
    let url = value.get("url_resolved")?.as_str()?;
    if !public_https(url) {
        return None;
    }
    let path = url.split('?').next()?.to_ascii_lowercase();
    if [".m3u", ".pls", ".asx", ".xspf", ".mpd"]
        .iter()
        .any(|suffix| path.ends_with(suffix))
    {
        return None;
    }
    let format = if hls == 1 || path.ends_with(".m3u8") {
        "hls"
    } else {
        "progressive"
    };
    Some(Station {
        id,
        title: title.to_owned(),
        url: url.to_owned(),
        format,
    })
}
fn page_state(context: &Map<String, Value>, key: &str, initial: usize) -> Option<(usize, usize)> {
    let Some(cursor) = context.get("cursor") else {
        return Some((0, initial));
    };
    let cursor = cursor.as_object()?;
    let offset = usize::try_from(cursor.get("offset")?.as_u64()?).ok()?;
    let mirror = usize::try_from(cursor.get("mirror")?.as_u64()?).ok()?;
    (cursor.len() == 4
        && cursor.get("v")?.as_u64()? == 1
        && cursor.get("key")?.as_str()? == key
        && offset > 0
        && offset <= MAX_OFFSET
        && offset % PAGE == 0
        && mirror < MIRRORS.len())
    .then_some((offset, mirror))
}
impl RadioBrowser {
    fn finish(&mut self, invocation: &str, status: &str, data: Value, error: &str) -> Vec<Value> {
        self.active.remove(invocation);
        self.pending.cancel(invocation);
        vec![result(invocation, status, data, error)]
    }
    fn request(&mut self, lookup: Lookup) -> Vec<Value> {
        let Some(sequence) = self.sequence.checked_add(1) else {
            return self.finish(
                &lookup.invocation,
                "error",
                json!({}),
                "The request limit was reached.",
            );
        };
        self.sequence = sequence;
        let id = format!("radio-http-{sequence}");
        let invocation = lookup.invocation.clone();
        let url = format!("{}{}", MIRRORS[lookup.mirror], lookup.path);
        if self.pending.insert(&id, &invocation, lookup).is_err() {
            return self.finish(
                &invocation,
                "error",
                json!({}),
                "The source has too many active requests.",
            );
        }
        vec![
            json!({"type":"host-call","id":id,"invocation":invocation,"operation":"network.http",
            "request":{"url":url,"method":"GET","headers":{"Accept":"application/json"},"responseBytes":MAX_BODY}}),
        ]
    }
    fn retry(&mut self, mut lookup: Lookup) -> Vec<Value> {
        lookup.attempts += 1;
        if lookup.attempts >= MAX_ATTEMPTS {
            return self.finish(
                &lookup.invocation,
                "error",
                json!({}),
                "The radio service did not respond. Try again.",
            );
        }
        lookup.mirror = (lookup.mirror + 1) % MIRRORS.len();
        self.request(lookup)
    }
    fn invoke(
        &mut self,
        id: &str,
        contribution: &str,
        operation: &str,
        context: &Map<String, Value>,
        capabilities: &Map<String, Value>,
    ) -> Vec<Value> {
        if self.active.contains(id) {
            return vec![];
        }
        if contribution != "stations" {
            return vec![result(id, "unsupported", json!({}), "")];
        }
        if operation == "roots" {
            if context.contains_key("cursor") {
                return vec![result(
                    id,
                    "error",
                    json!({}),
                    "The page reference is not valid.",
                )];
            }
            return vec![result(
                id,
                "match",
                json!({"items":[
                {"id":"popular","kind":"node","title":"Stations"},
                {"id":"jazz","kind":"node","title":"Jazz"},
                {"id":"classical","kind":"node","title":"Classical music"},
                {"id":"news","kind":"node","title":"News"}]}),
                "",
            )];
        }
        if self.active.len() >= MAX_ACTIVE {
            return vec![result(
                id,
                "error",
                json!({}),
                "The source has too many active requests.",
            )];
        }
        let initial = self.next_mirror;
        self.next_mirror = (initial + 1) % MIRRORS.len();
        let (path, kind, mirror) = match operation {
            "browse" | "search" => {
                let (key, filter) = if operation == "browse" {
                    match context.get("nodeRef").and_then(Value::as_str).unwrap_or("") {
                        "popular" => ("browse:popular".to_owned(), String::new()),
                        name @ ("jazz" | "classical" | "news") => (
                            format!("browse:{name}"),
                            format!("&tag={name}&tagExact=true"),
                        ),
                        _ => return vec![result(id, "no_match", json!({"items":[]}), "")],
                    }
                } else {
                    if context
                        .get("filters")
                        .is_some_and(|value| value.as_object().is_none_or(|map| !map.is_empty()))
                    {
                        return vec![result(
                            id,
                            "unsupported",
                            json!({}),
                            "This source supports station names only.",
                        )];
                    }
                    let Some(query) = context.get("query").and_then(Value::as_str) else {
                        return vec![result(id, "error", json!({}), "Enter a station name.")];
                    };
                    let query = query.trim();
                    if query.is_empty()
                        || query.encode_utf16().count() > 512
                        || query.chars().any(char::is_control)
                    {
                        return vec![result(
                            id,
                            "error",
                            json!({}),
                            "Enter a station name with at most 512 characters.",
                        )];
                    }
                    (
                        format!("search:{query}"),
                        format!("&name={}", encode(query)),
                    )
                };
                let Some((offset, mirror)) = page_state(context, &key, initial) else {
                    return vec![result(
                        id,
                        "error",
                        json!({}),
                        "The page reference is not valid.",
                    )];
                };
                (format!("/json/stations/search?is_https=true&hidebroken=true&order=clickcount&reverse=true&limit={PAGE}&offset={offset}{filter}"), Kind::Page { key, offset }, mirror)
            }
            "resolve" => {
                if capabilities
                    .get("mediaStreamAvailable")
                    .and_then(Value::as_bool)
                    != Some(true)
                    || context.get("intent").and_then(Value::as_str) != Some("play")
                {
                    return vec![result(
                        id,
                        "unsupported",
                        json!({}),
                        "Source playback is not available.",
                    )];
                }
                let Some(station) = context
                    .get("itemRef")
                    .and_then(Value::as_str)
                    .and_then(uuid)
                else {
                    return vec![result(id, "no_match", json!({}), "")];
                };
                (
                    format!("/json/stations/byuuid?uuids={station}"),
                    Kind::Resolve { station },
                    initial,
                )
            }
            _ => return vec![result(id, "unsupported", json!({}), "")],
        };
        self.active.insert(id.to_owned());
        self.request(Lookup {
            invocation: id.to_owned(),
            path,
            kind,
            mirror,
            attempts: 0,
        })
    }
    fn complete(
        &mut self,
        id: &str,
        invocation: &str,
        ok: bool,
        response: Option<&Map<String, Value>>,
    ) -> Vec<Value> {
        let Some(lookup) = self.pending.take(id, invocation) else {
            return vec![];
        };
        let status = response
            .and_then(|r| r.get("status"))
            .and_then(Value::as_u64)
            .unwrap_or(0);
        if !ok || status == 0 || status == 429 || status >= 500 {
            return self.retry(lookup);
        }
        if status == 404 {
            return self.finish(invocation, "no_match", json!({"items":[]}), "");
        }
        if status != 200 {
            return self.finish(
                invocation,
                "error",
                json!({}),
                "The radio service refused the request.",
            );
        }
        if response
            .and_then(|r| r.get("truncated"))
            .is_some_and(|value| value != &Value::Bool(false))
        {
            return self.finish(
                invocation,
                "error",
                json!({}),
                "The radio response is incomplete.",
            );
        }
        let body = response
            .and_then(|r| r.get("body"))
            .and_then(Value::as_str)
            .unwrap_or("");
        if body.len() > MAX_BODY {
            return self.finish(
                invocation,
                "error",
                json!({}),
                "The radio response is too large.",
            );
        }
        let Ok(value) = hakone_addon_message_v1::serde_json::from_str::<Value>(body) else {
            return self.finish(
                invocation,
                "error",
                json!({}),
                "The radio response is not valid JSON.",
            );
        };
        let Some(rows) = value.as_array() else {
            return self.finish(
                invocation,
                "error",
                json!({}),
                "The radio response is not a station list.",
            );
        };
        match &lookup.kind {
            Kind::Page { key, offset } => {
                if rows.len() > PAGE {
                    return self.finish(
                        invocation,
                        "error",
                        json!({}),
                        "The station list is too large.",
                    );
                }
                let mut seen = BTreeSet::new();
                let items: Vec<_> = rows
                    .iter()
                    .filter_map(station)
                    .filter(|s| seen.insert(s.id.clone()))
                    .map(|s| json!({"id":s.id,"kind":"media","title":s.title,"mediaKind":"audio"}))
                    .collect();
                let more = rows.len() == PAGE && *offset < MAX_OFFSET;
                let status = if items.is_empty() && !more {
                    "no_match"
                } else {
                    "match"
                };
                let mut data = json!({"items":items});
                if more {
                    data["cursor"] =
                        json!({"v":1,"key":key,"offset":offset+PAGE,"mirror":lookup.mirror});
                }
                self.finish(invocation, status, data, "")
            }
            Kind::Resolve { station: expected } => {
                if rows.len() != 1 {
                    return self.finish(invocation, "no_match", json!({}), "");
                }
                let Some(value) = station(&rows[0]).filter(|s| &s.id == expected) else {
                    return self.finish(
                        invocation,
                        "no_match",
                        json!({}),
                        "The station has no supported public HTTPS stream.",
                    );
                };
                self.finish(
                    invocation,
                    "match",
                    json!({"url":value.url,"format":value.format}),
                    "",
                )
            }
        }
    }
}
impl Addon for RadioBrowser {
    fn handle(&mut self, message: Incoming<'_>) -> Vec<Value> {
        match message {
            Incoming::Invoke {
                id,
                contribution,
                operation,
                context,
                capabilities,
                ..
            } => self.invoke(id, contribution, operation, context, capabilities),
            Incoming::HostResult {
                id,
                invocation,
                ok,
                response,
                ..
            } => self.complete(id, invocation, ok, response),
            Incoming::Cancel { id, .. } => {
                self.pending.cancel(id);
                self.active.remove(id);
                vec![]
            }
        }
    }
}
hakone_addon_message_v1::export_addon!(RadioBrowser, RadioBrowser::default());

#[cfg(test)]
mod tests;

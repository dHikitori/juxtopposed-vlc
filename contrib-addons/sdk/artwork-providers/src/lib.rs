// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2007, 2010 VideoLAN and VLC authors.
//! Portable artwork protocols. The host owns HTTP and image files.
pub use serde_json;
use serde_json::{json, Value};
use std::collections::BTreeMap;
pub use hakone_addon_message_v1 as message_v1;
mod musicbrainz;

pub const MAX_INPUT: usize = message_v1::MAX_MESSAGE;
const MAX_PENDING: usize = 8;
const MAX_ISSUED: usize = 2048;
#[derive(Clone, Copy)]
pub enum Kind {
    FrenchTv,
    LastFm,
    GoogleImages,
    MusicBrainz,
}
#[derive(Clone)]
struct Pending {
    invocation: String,
    identity: Value,
    operation: String,
    source: String,
}
pub struct Provider {
    kind: Kind,
    sequence: u64,
    pending: BTreeMap<String, Pending>,
    issued: BTreeMap<String, Value>,
    musicbrainz: musicbrainz::Provider,
}
pub fn text(value: &Value, field: &str) -> String {
    value
        .get(field)
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_owned()
}
pub fn encode(value: &str) -> String {
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
fn result(id: &str, status: &str, data: Value, error: &str) -> Vec<Value> {
    let mut value = json!({"type":"result","id":id,"status":status,"data":data});
    if !error.is_empty() {
        value["error"] = json!(error);
    }
    vec![value]
}
fn label_record(identity: &Value, url: &str, name: &str, source: &str) -> Value {
    json!({"identity":identity,"fields":{"artwork":{"url":url}},"evidence":{"type":"label"},
        "attribution":{"name":name,"url":source}})
}
fn image_url(value: &str, hosts: &[&str]) -> bool {
    if value.len() > 4096 || value.contains(['\r', '\n', ' ', '\\', '#']) {
        return false;
    }
    let Some(path) = value.strip_prefix("https://") else {
        return false;
    };
    let host = path.split('/').next().unwrap_or("");
    hosts
        .iter()
        .any(|allowed| host.eq_ignore_ascii_case(allowed))
        && path.contains('/')
}
fn unescape(value: &str) -> String {
    value
        .replace("&amp;", "&")
        .replace("&quot;", "\"")
        .replace("&#39;", "'")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
}
fn attributes(tag: &str) -> BTreeMap<String, String> {
    let bytes = tag.as_bytes();
    let mut index = 0;
    let mut result = BTreeMap::new();
    while index < bytes.len() {
        while index < bytes.len() && !(bytes[index].is_ascii_alphanumeric() || bytes[index] == b'_')
        {
            index += 1;
        }
        let start = index;
        while index < bytes.len()
            && (bytes[index].is_ascii_alphanumeric() || b"_:-".contains(&bytes[index]))
        {
            index += 1;
        }
        let name = tag[start..index].to_ascii_lowercase();
        while index < bytes.len() && bytes[index].is_ascii_whitespace() {
            index += 1;
        }
        if index >= bytes.len() || bytes[index] != b'=' {
            continue;
        }
        index += 1;
        while index < bytes.len() && bytes[index].is_ascii_whitespace() {
            index += 1;
        }
        if index >= bytes.len() {
            break;
        }
        let quote = bytes[index];
        let quoted = quote == b'\'' || quote == b'"';
        if quoted {
            index += 1;
        }
        let start = index;
        while index < bytes.len()
            && if quoted {
                bytes[index] != quote
            } else {
                !bytes[index].is_ascii_whitespace() && bytes[index] != b'>'
            }
        {
            index += 1;
        }
        if !name.is_empty() && index - start <= 4096 {
            result.insert(name, unescape(&tag[start..index]));
        }
        if quoted && index < bytes.len() {
            index += 1;
        }
    }
    result
}
fn tags<'a>(html: &'a str, name: &str) -> Vec<&'a str> {
    let lower = html.to_ascii_lowercase();
    let needle = format!("<{name}");
    let mut offset = 0;
    let mut result = Vec::new();
    while let Some(start) = lower[offset..].find(&needle) {
        let start = offset + start;
        let Some(end) = lower[start..].find('>') else {
            break;
        };
        offset = start + end + 1;
        if end <= 8192 {
            result.push(&html[start..offset]);
        }
        if result.len() >= 512 || offset >= lower.len() {
            break;
        }
    }
    result
}
fn album_name(value: &str) -> String {
    let value = value.split(" -").next().unwrap_or(value);
    let mut cleaned = String::new();
    let mut depth: usize = 0;
    for character in value.chars() {
        if character == '(' {
            depth += 1;
        } else if character == ')' && depth > 0 {
            depth -= 1;
        } else if depth == 0 {
            cleaned.push(character);
        }
    }
    let mut words = cleaned.split_whitespace();
    let mut result = Vec::new();
    while let Some(word) = words.next() {
        if word == "Disc" {
            words.next();
            continue;
        }
        if word
            .strip_prefix("CD")
            .is_some_and(|tail| !tail.is_empty() && tail.bytes().all(|b| b.is_ascii_digit()))
        {
            continue;
        }
        result.push(word);
    }
    result.join(" ")
}

impl Provider {
    pub fn new(kind: Kind) -> Self {
        Self {
            kind,
            sequence: 0,
            pending: BTreeMap::new(),
            issued: BTreeMap::new(),
            musicbrainz: musicbrainz::Provider::new(),
        }
    }
    pub fn handle(&mut self, message: &Value) -> Vec<Value> {
        if matches!(self.kind, Kind::MusicBrainz) {
            return self.musicbrainz.handle(message);
        }
        match text(message, "type").as_str() {
            "invoke" => self.invoke(message),
            "host-result" => self.complete(message),
            "cancel" => {
                let id = text(message, "id");
                self.pending.retain(|_, request| request.invocation != id);
                Vec::new()
            }
            _ => Vec::new(),
        }
    }
    fn invoke(&mut self, message: &Value) -> Vec<Value> {
        let id = text(message, "id");
        let operation = text(message, "operation");
        if id.is_empty() || id.len() > 256 {
            return Vec::new();
        }
        if text(message, "contribution") != "artwork" {
            return result(
                &id,
                "unsupported",
                json!({}),
                "This artwork contribution does not exist.",
            );
        }
        if operation == "resolveAsset" {
            let candidate = text(&message["context"]["candidate"], "id");
            return if let Some(asset) = self.issued.get(&candidate) {
                result(&id, "match", asset.clone(), "")
            } else {
                result(
                    &id,
                    "error",
                    json!({}),
                    "Search again before the image download.",
                )
            };
        }
        if operation != "enrich" && operation != "searchAssets" {
            return result(
                &id,
                "unsupported",
                json!({}),
                "This provider supplies artwork only.",
            );
        }
        if operation == "enrich"
            && !message["context"]["fields"]
                .as_array()
                .is_some_and(|fields| fields.iter().any(|f| f == "artwork" || f == "artworkUrl"))
        {
            return result(
                &id,
                "unsupported",
                json!({}),
                "This provider requires an artwork field.",
            );
        }
        if self.pending.len() >= MAX_PENDING
            || self
                .pending
                .values()
                .any(|request| request.invocation == id)
        {
            return result(
                &id,
                "rate_limited",
                json!({"retryAfterSeconds":1}),
                "The artwork request limit has been reached.",
            );
        }
        let identity = message["context"]["identity"].clone();
        if !identity.is_object() {
            return result(
                &id,
                "no_match",
                json!({}),
                "The media identity is not available.",
            );
        }
        match self.kind {
            Kind::FrenchTv => {
                let mut channel = text(&identity, "title").trim().to_owned();
                if channel.ends_with("HD")
                    && channel[..channel.len() - 2]
                        .chars()
                        .last()
                        .is_some_and(char::is_whitespace)
                {
                    channel.truncate(channel.len() - 2);
                }
                channel = channel
                    .trim()
                    .strip_suffix("(bas débit)")
                    .unwrap_or(channel.trim())
                    .trim()
                    .to_owned();
                let channels: Value = serde_json::from_str(include_str!("french-tv.json")).unwrap();
                let Some(url) = channels.get(&channel).and_then(Value::as_str) else {
                    return result(&id, "no_match", json!({}), "");
                };
                let record = label_record(
                    &identity,
                    url,
                    "Wikimedia",
                    &format!("https://fr.wikipedia.org/wiki/{}", encode(&channel)),
                );
                self.assets(&id, &operation, vec![record])
            }
            Kind::LastFm => {
                if ["radio", "tv"].contains(&text(&identity["release"], "listingType").as_str()) {
                    return result(&id, "no_match", json!({}), "");
                }
                let artist = text(&identity, "artist");
                let album = album_name(&text(&identity, "album"));
                if artist.trim().is_empty()
                    || album.is_empty()
                    || artist.len() > 512
                    || album.len() > 512
                {
                    return result(
                        &id,
                        "no_match",
                        json!({}),
                        "The artist and album are not available.",
                    );
                }
                let url = format!(
                    "https://www.last.fm/music/{}/{}",
                    encode(&artist),
                    encode(&album)
                );
                self.request(id, identity, operation, url)
            }
            Kind::GoogleImages => {
                let artist = text(&identity, "artist");
                let album = text(&identity, "album");
                let title = text(&identity, "title");
                let listing = text(&identity["release"], "listingType");
                let query = if listing == "radio" || listing == "tv" {
                    format!("{title} {listing} logo")
                } else if !artist.is_empty() && !album.is_empty() {
                    format!("{artist} {album} cover")
                } else if !artist.is_empty() && !title.is_empty() {
                    format!("{artist} {title} cover")
                } else {
                    return result(
                        &id,
                        "no_match",
                        json!({}),
                        "The artwork search title is not available.",
                    );
                };
                if query.len() > 1024 {
                    return result(
                        &id,
                        "error",
                        json!({}),
                        "The artwork query exceeds the size limit.",
                    );
                }
                self.request(
                    id,
                    identity,
                    operation,
                    format!("https://images.google.com/images?q={}", encode(&query)),
                )
            }
            Kind::MusicBrainz => result(
                &id,
                "unsupported",
                json!({}),
                "This artwork protocol is not available in this build.",
            ),
        }
    }
    fn request(
        &mut self,
        invocation: String,
        identity: Value,
        operation: String,
        source: String,
    ) -> Vec<Value> {
        self.sequence += 1;
        let id = format!("artwork-http-{}", self.sequence);
        let call = json!({"type":"host-call","id":id,"invocation":invocation,"operation":"network.http",
            "request":{"url":source,"method":"GET","headers":{"Accept":"text/html"},"responseBytes":65653,"allowTruncated":true}});
        self.pending.insert(
            id,
            Pending {
                invocation,
                identity,
                operation,
                source,
            },
        );
        vec![call]
    }
    fn complete(&mut self, message: &Value) -> Vec<Value> {
        let key = text(message, "id");
        let Some(pending) = self.pending.get(&key) else {
            return Vec::new();
        };
        if text(message, "invocation") != pending.invocation {
            return Vec::new();
        }
        let request = self.pending.remove(&key).unwrap();
        if text(message, "status") != "ok" {
            return result(
                &request.invocation,
                "error",
                json!({}),
                "The artwork request failed.",
            );
        }
        let response = &message["response"];
        let status = response["status"].as_u64().unwrap_or(0);
        if status == 429 || status == 503 {
            return result(
                &request.invocation,
                "rate_limited",
                json!({"retryAfterSeconds":10}),
                "The image service requires a delay before another request.",
            );
        }
        if status != 200 {
            return result(
                &request.invocation,
                "no_match",
                json!({}),
                "The image service did not return an artwork page.",
            );
        }
        let body = text(response, "body");
        if body.len() > 65653 {
            return result(
                &request.invocation,
                "error",
                json!({}),
                "The artwork page exceeds the size limit.",
            );
        }
        let mut records = Vec::new();
        match self.kind {
            Kind::LastFm => {
                for tag in tags(&body, "meta") {
                    let attributes = attributes(tag);
                    if attributes
                        .get("property")
                        .is_none_or(|value| value != "og:image")
                    {
                        continue;
                    }
                    let Some(url) = attributes.get("content") else {
                        continue;
                    };
                    if !image_url(
                        url,
                        &[
                            "lastfm-img.freetls.fastly.net",
                            "lastfm.freetls.fastly.net",
                            "lastfm-img2.akamaized.net",
                        ],
                    ) || url.contains("default_album_mega.png")
                    {
                        continue;
                    }
                    records.push(label_record(
                        &request.identity,
                        &url.replace("/i/u/ar0/", "/i/u/500x500/"),
                        "Last.fm",
                        &request.source,
                    ));
                }
            }
            Kind::GoogleImages => {
                for tag in tags(&body, "img") {
                    let attributes = attributes(tag);
                    let Some(url) = attributes.get("src") else {
                        continue;
                    };
                    if image_url(
                        url,
                        &[
                            "encrypted-tbn0.gstatic.com",
                            "encrypted-tbn1.gstatic.com",
                            "encrypted-tbn2.gstatic.com",
                            "encrypted-tbn3.gstatic.com",
                            "images.gstatic.com",
                        ],
                    ) && records.len() < 32
                    {
                        records.push(label_record(
                            &request.identity,
                            url,
                            "Google Images",
                            &request.source,
                        ));
                    }
                }
            }
            _ => (),
        }
        self.assets(&request.invocation, &request.operation, records)
    }
    fn assets(&mut self, id: &str, operation: &str, records: Vec<Value>) -> Vec<Value> {
        if records.is_empty() {
            return result(id, "no_match", json!({}), "");
        }
        if operation == "enrich" {
            return result(id, "match", json!({"records":records}), "");
        }
        let mut candidates = Vec::new();
        for record in records {
            if self.issued.len() >= MAX_ISSUED {
                break;
            }
            self.sequence += 1;
            let key = format!("image-{}", self.sequence);
            let asset = record["fields"]["artwork"].clone();
            self.issued.insert(key.clone(), asset);
            candidates.push(json!({"id":key,"name":text(&record["identity"],"title"),"identity":record["identity"],
                "evidence":record["evidence"],"attribution":record["attribution"]}));
        }
        if candidates.is_empty() {
            return result(
                id,
                "error",
                json!({}),
                "The retained artwork result limit has been reached.",
            );
        }
        result(id, "match", json!({"candidates":candidates}), "")
    }
}

impl message_v1::Addon for Provider {
    fn handle(&mut self, message: message_v1::Incoming<'_>) -> Vec<Value> {
        Provider::handle(self, message.value())
    }
}
#[macro_export]
macro_rules! export_provider {
    ($kind:expr) => {
        $crate::message_v1::export_addon!($crate::Provider, $crate::Provider::new($kind));
    };
}

#[cfg(test)]
mod tests;

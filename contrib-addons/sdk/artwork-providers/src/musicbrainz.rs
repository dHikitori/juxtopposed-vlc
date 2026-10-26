// SPDX-License-Identifier: GPL-2.0-or-later
use super::{encode, result, text, Value, MAX_ISSUED, MAX_PENDING};
use serde_json::json;
use std::collections::BTreeMap;

#[derive(Clone)]
enum Stage {
    Releases,
    Recordings,
    Release {
        id: String,
        remaining: Vec<String>,
        records: Vec<Value>,
        direct: bool,
    },
}
#[derive(Clone)]
struct Pending {
    invocation: String,
    identity: Value,
    operation: String,
    stage: Stage,
}
pub struct Provider {
    sequence: u64,
    pending: BTreeMap<String, Pending>,
    issued: BTreeMap<String, Value>,
}
fn uuid(value: &str) -> bool {
    value.len() == 36
        && value.bytes().enumerate().all(|(i, c)| {
            if [8, 13, 18, 23].contains(&i) {
                c == b'-'
            } else {
                c.is_ascii_hexdigit()
            }
        })
}
fn normalized(value: &str) -> String {
    value
        .chars()
        .flat_map(char::to_lowercase)
        .map(|c| if c.is_alphanumeric() { c } else { ' ' })
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
}
fn escaped(value: &str) -> String {
    value.replace('\\', "\\\\").replace('"', "\\\"")
}
fn artist(value: &Value) -> String {
    value["artist-credit"]
        .as_array()
        .map(|credits| {
            credits
                .iter()
                .map(|credit| {
                    let mut name = text(credit, "name");
                    if name.is_empty() {
                        name = text(&credit["artist"], "name");
                    }
                    name + &text(credit, "joinphrase")
                })
                .collect::<String>()
        })
        .unwrap_or_default()
}
fn same(expected: &str, actual: &str) -> bool {
    !expected.trim().is_empty() && normalized(expected) == normalized(actual)
}
fn release_matches(identity: &Value, release: &Value) -> bool {
    let album = text(identity, "album");
    let expected = if text(identity, "kind") == "album" {
        text(identity, "title")
    } else {
        album
    };
    let actual_artist = artist(release);
    (expected.is_empty() || same(&expected, &text(release, "title")))
        && ["artist", "albumArtist"].iter().all(|field| {
            let expected = text(identity, field);
            expected.is_empty() || same(&expected, &actual_artist)
        })
}
fn asin(value: &str) -> bool {
    value.len() == 10
        && value
            .bytes()
            .all(|c| c.is_ascii_uppercase() || c.is_ascii_digit())
}

impl Provider {
    pub fn new() -> Self {
        Self {
            sequence: 0,
            pending: BTreeMap::new(),
            issued: BTreeMap::new(),
        }
    }
    pub fn handle(&mut self, message: &Value) -> Vec<Value> {
        match text(message, "type").as_str() {
            "invoke" => self.invoke(message),
            "host-result" => self.complete(message),
            "cancel" => {
                let id = text(message, "id");
                self.pending.retain(|_, p| p.invocation != id);
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
            return self
                .issued
                .get(&text(&message["context"]["candidate"], "id"))
                .map(|asset| result(&id, "match", asset.clone(), ""))
                .unwrap_or_else(|| {
                    result(
                        &id,
                        "error",
                        json!({}),
                        "Search again before the image download.",
                    )
                });
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
        if self.pending.len() >= MAX_PENDING || self.pending.values().any(|p| p.invocation == id) {
            return result(
                &id,
                "rate_limited",
                json!({"retryAfterSeconds":1}),
                "The artwork request limit has been reached.",
            );
        }
        let identity = message["context"]["identity"].clone();
        let listing = text(&identity["release"], "listingType");
        if !identity.is_object() || listing == "radio" || listing == "tv" {
            return result(&id, "no_match", json!({}), "");
        }
        let release = text(&identity["ids"], "musicBrainzAlbumId");
        let manual = message["context"]["manual"].as_bool().unwrap_or(false);
        let query = text(&message["context"], "query").trim().to_owned();
        if uuid(&release) && !(manual && !query.is_empty()) {
            return self.release(
                Pending {
                    invocation: id,
                    identity,
                    operation,
                    stage: Stage::Releases,
                },
                release.to_ascii_lowercase(),
                Vec::new(),
                Vec::new(),
                true,
            );
        }
        let artist = text(&identity, "artist");
        let album = text(&identity, "album");
        let title = text(&identity, "title");
        let (query, stage) = if manual && !query.is_empty() {
            (query, Stage::Releases)
        } else if !artist.is_empty() && !album.is_empty() {
            (
                format!(
                    "artist:\"{}\" AND release:\"{}\"",
                    escaped(&artist),
                    escaped(&album)
                ),
                Stage::Releases,
            )
        } else if !artist.is_empty() && !title.is_empty() {
            (
                format!(
                    "artist:\"{}\" AND recording:\"{}\"",
                    escaped(&artist),
                    escaped(&title)
                ),
                Stage::Recordings,
            )
        } else {
            return result(
                &id,
                "no_match",
                json!({}),
                "The artist and album or title are not available.",
            );
        };
        if query.len() > 2048 {
            return result(
                &id,
                "error",
                json!({}),
                "The artwork query exceeds the size limit.",
            );
        }
        let entity = if matches!(stage, Stage::Recordings) {
            "recording"
        } else {
            "release"
        };
        let url = format!(
            "https://musicbrainz.org/ws/2/{entity}/?query={}&fmt=json&limit=8",
            encode(&query)
        );
        self.request(
            Pending {
                invocation: id,
                identity,
                operation,
                stage,
            },
            url,
        )
    }
    fn request(&mut self, pending: Pending, url: String) -> Vec<Value> {
        self.sequence += 1;
        let id = format!("musicbrainz-http-{}", self.sequence);
        let call = json!({"type":"host-call","id":id,"invocation":pending.invocation,"operation":"network.http",
            "request":{"url":url,"method":"GET","headers":{"Accept":"application/json"}}});
        self.pending.insert(id, pending);
        vec![call]
    }
    fn release(
        &mut self,
        mut pending: Pending,
        id: String,
        remaining: Vec<String>,
        records: Vec<Value>,
        direct: bool,
    ) -> Vec<Value> {
        let url = format!("https://musicbrainz.org/ws/2/release/{id}?inc=artists&fmt=json");
        pending.stage = Stage::Release {
            id,
            remaining,
            records,
            direct,
        };
        self.request(pending, url)
    }
    fn complete(&mut self, message: &Value) -> Vec<Value> {
        let key = text(message, "id");
        let Some(pending) = self.pending.get(&key) else {
            return Vec::new();
        };
        if text(message, "invocation") != pending.invocation {
            return Vec::new();
        }
        let pending = self.pending.remove(&key).unwrap();
        if text(message, "status") != "ok" {
            return result(
                &pending.invocation,
                "error",
                json!({}),
                "The artwork request failed.",
            );
        }
        let response = &message["response"];
        let status = response["status"].as_u64().unwrap_or(0);
        if status == 429 || status == 503 {
            return result(
                &pending.invocation,
                "rate_limited",
                json!({"retryAfterSeconds":10}),
                "The image service requires a delay before another request.",
            );
        }
        let body = text(response, "body");
        if body.len() > 512 * 1024 || response["truncated"].as_bool().unwrap_or(false) {
            return result(
                &pending.invocation,
                "error",
                json!({}),
                "The MusicBrainz response exceeds the size limit.",
            );
        }
        let document: Value = if status == 200 {
            match serde_json::from_str(&body) {
                Ok(value) => value,
                Err(_) => {
                    return result(
                        &pending.invocation,
                        "error",
                        json!({}),
                        "The MusicBrainz JSON response is not correct.",
                    )
                }
            }
        } else {
            Value::Null
        };
        match pending.stage.clone() {
            Stage::Releases | Stage::Recordings => {
                let mut ids = Vec::new();
                if let Some(releases) = document["releases"].as_array() {
                    for release in releases.iter().take(8) {
                        let id = text(release, "id");
                        if uuid(&id)
                            && release_matches(&pending.identity, release)
                            && !ids.contains(&id)
                        {
                            ids.push(id);
                        }
                    }
                } else if let Some(recordings) = document["recordings"].as_array() {
                    for recording in recordings.iter().take(8) {
                        if !same(&text(&pending.identity, "title"), &text(recording, "title"))
                            || !same(&text(&pending.identity, "artist"), &artist(recording))
                        {
                            continue;
                        }
                        if let Some(releases) = recording["releases"].as_array() {
                            for release in releases.iter().take(8) {
                                let id = text(release, "id");
                                if uuid(&id) && !ids.contains(&id) && ids.len() < 8 {
                                    ids.push(id);
                                }
                            }
                        }
                    }
                }
                if ids.is_empty() {
                    return result(&pending.invocation, "no_match", json!({}), "");
                }
                let first = ids.remove(0);
                self.release(pending, first, ids, Vec::new(), false)
            }
            Stage::Release {
                id,
                mut remaining,
                mut records,
                direct,
            } => {
                if text(&document, "id").eq_ignore_ascii_case(&id)
                    && release_matches(&pending.identity, &document)
                {
                    let archived = document["cover-art-archive"]["front"]
                        .as_bool()
                        .unwrap_or(false);
                    let url = if archived {
                        format!("https://coverartarchive.org/release/{id}/front-500")
                    } else {
                        let code = text(&document, "asin");
                        if asin(&code) {
                            format!("https://images-na.ssl-images-amazon.com/images/P/{code}.01._SCLZZZZZZZ_.jpg")
                        } else {
                            String::new()
                        }
                    };
                    if !url.is_empty() {
                        records.push(json!({"identity":pending.identity,"fields":{"artwork":{"url":url}},
                            "evidence":{"type":if direct {"identifier"} else {"label"}},
                            "attribution":{"name":if archived {"MusicBrainz and Cover Art Archive"} else {"MusicBrainz and Amazon"},"url":format!("https://musicbrainz.org/release/{id}")}}));
                    }
                }
                if !remaining.is_empty() {
                    let first = remaining.remove(0);
                    return self.release(pending, first, remaining, records, direct);
                }
                self.assets(&pending.invocation, &pending.operation, records)
            }
        }
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
            let key = format!("musicbrainz-image-{}", self.sequence);
            self.issued
                .insert(key.clone(), record["fields"]["artwork"].clone());
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

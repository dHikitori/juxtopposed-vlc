// SPDX-License-Identifier: GPL-2.0-or-later
//! A metadata provider template: enrich a movie with its description.
//!
//! The host owns the network. The add-on asks for one HTTPS request and
//! turns the reply into fields. Replace the URL, the reply parsing and the
//! attribution with your service's.
use hakone_addon_message_v1::{
    contract::{result, Keep, Retention, Status},
    serde_json::{json, Map, Value},
    Addon, Incoming, Pending,
};

const ORIGIN: &str = "https://api.example.org";
const MAX_BODY: usize = 64 * 1024;

struct Lookup {
    invocation: String,
}

pub struct Provider {
    pending: Pending<Lookup>,
    sequence: u64,
}

impl Default for Provider {
    fn default() -> Self {
        Self { pending: Pending::new(16), sequence: 0 }
    }
}

fn encode(value: &str) -> String {
    value
        .bytes()
        .map(|byte| {
            if byte.is_ascii_alphanumeric() || b"-._~".contains(&byte) {
                (byte as char).to_string()
            } else {
                format!("%{byte:02X}")
            }
        })
        .collect()
}

fn failed(id: &str, status: Status, error: &str) -> Vec<Value> {
    let mut message = result(id, status, json!({}));
    message["error"] = json!(error);
    vec![message]
}

/// The service's data license decides retention: here one hour of reuse,
/// saving accepted fields, and one day of offline use.
fn retention() -> Value {
    Retention { reuse_seconds: Some(3600), keep: Some(Keep::Library), offline_seconds: Some(86400) }.to_value()
}

/// Reply parsing without the network, so that the self-test and unit tests
/// use the same code.
pub fn parse(body: &str, identity: &Value) -> Option<Value> {
    let reply: Value = hakone_addon_message_v1::serde_json::from_str(body).ok()?;
    let description = reply.get("overview")?.as_str()?.trim();
    if description.is_empty() {
        return None;
    }
    Some(json!({
        "identity": identity,
        "fields": {"description": description},
        "attribution": {"name": "Example service", "url": ORIGIN, "license": "CC-BY-4.0"},
        "retention": retention()
    }))
}

impl Provider {
    fn enrich(&mut self, id: &str, context: &Map<String, Value>) -> Vec<Value> {
        let identity = context.get("identity").cloned().unwrap_or(json!({}));
        let Some(title) = identity.get("title").and_then(Value::as_str).filter(|title| !title.trim().is_empty()) else {
            return vec![result(id, Status::Unsupported, json!({}))];
        };
        self.sequence += 1;
        let call = format!("http-{}", self.sequence);
        if self.pending.insert(&call, id, Lookup { invocation: id.into() }).is_err() {
            return failed(id, Status::Error, "The provider has too many active requests.");
        }
        let url = format!("{ORIGIN}/movies?title={}", encode(title));
        vec![json!({"type": "host-call", "id": call, "invocation": id, "operation": "network.http",
            "request": {"url": url, "method": "GET", "headers": {"Accept": "application/json"}, "responseBytes": MAX_BODY}})]
    }

    fn complete(&mut self, call: &str, invocation: &str, ok: bool, response: Option<&Map<String, Value>>) -> Vec<Value> {
        let Some(lookup) = self.pending.take(call, invocation) else {
            return vec![];
        };
        let status = response.and_then(|reply| reply.get("status")).and_then(Value::as_u64).unwrap_or(0);
        if !ok || status == 0 {
            return failed(&lookup.invocation, Status::Offline, "The service cannot be reached.");
        }
        if status == 404 {
            return vec![result(&lookup.invocation, Status::NoMatch, json!({}))];
        }
        if status == 429 {
            return vec![result(&lookup.invocation, Status::RateLimited, json!({"retryAfterSeconds": 60}))];
        }
        let body = response.and_then(|reply| reply.get("body")).and_then(Value::as_str).unwrap_or("");
        match parse(body, &json!({})) {
            Some(data) => vec![result(&lookup.invocation, Status::Partial, data)],
            None => vec![result(&lookup.invocation, Status::NoMatch, json!({}))],
        }
    }
}

impl Addon for Provider {
    /// The install check: parse a built-in sample reply, with no network.
    fn self_test(&mut self) -> bool {
        parse(r#"{"overview":"A sample film."}"#, &json!({"kind": "movie", "title": "Sample"}))
            .is_some_and(|data| data["fields"]["description"] == "A sample film.")
    }
    fn handle(&mut self, message: Incoming<'_>) -> Vec<Value> {
        match message {
            Incoming::Invoke { id, operation, context, .. } if operation == "enrich" => self.enrich(id, context),
            Incoming::Invoke { id, .. } => vec![result(id, Status::Unsupported, json!({}))],
            Incoming::HostResult { id, invocation, ok, response, .. } => self.complete(id, invocation, ok, response),
            Incoming::Cancel { id, .. } => {
                self.pending.cancel(id);
                vec![]
            }
        }
    }
}

hakone_addon_message_v1::export_addon!(Provider, Provider::default());

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_reply_becomes_fields_with_retention() {
        let data = parse(r#"{"overview":"A film."}"#, &json!({"kind": "movie", "title": "Example"})).unwrap();
        assert_eq!(data["fields"]["description"], "A film.");
        assert_eq!(data["retention"]["keep"], "library");
        assert!(parse(r#"{"overview":""}"#, &json!({})).is_none());
    }

    #[test]
    fn enrich_asks_for_one_request_and_maps_the_answer() {
        let mut provider = Provider::default();
        let context = json!({"identity": {"kind": "movie", "title": "Example Film"}});
        let calls = provider.enrich("job-1", context.as_object().unwrap());
        assert_eq!(calls[0]["request"]["url"], "https://api.example.org/movies?title=Example%20Film");
        let call = calls[0]["id"].as_str().unwrap().to_string();
        let reply = json!({"status": 200, "body": r#"{"overview":"A film."}"#});
        let answer = provider.complete(&call, "job-1", true, reply.as_object());
        assert_eq!(answer[0]["status"], "partial");
        assert!(provider.complete(&call, "job-1", true, reply.as_object()).is_empty());
    }
}

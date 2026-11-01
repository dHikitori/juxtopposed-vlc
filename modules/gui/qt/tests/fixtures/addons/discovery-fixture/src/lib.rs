// SPDX-License-Identifier: GPL-2.0-or-later
//! Synthetic source for testing portable discovery; it never plays real media.
use serde_json::{json, Value};
const MAX_INPUT: usize = 1024 * 1024;

fn examples() -> Vec<Value> {
    vec![
        json!({"id":"sample-jazz","kind":"media","title":"Synthetic jazz station","mediaKind":"musicTrack"}),
        json!({"id":"sample-talk","kind":"media","title":"Synthetic talk station","mediaKind":"musicTrack"}),
        json!({"id":"sample-podcast","kind":"media","title":"Synthetic podcast episode","mediaKind":"podcastEpisode"}),
    ]
}

pub fn handle(message: &Value) -> Option<Value> {
    if message.get("type").and_then(Value::as_str) != Some("invoke") {
        return None;
    }
    let id = message.get("id").and_then(Value::as_str)?;
    let mut reply = json!({"type":"result","id":id,"status":"match","data":{"items":[]}});
    if message.get("contribution").and_then(Value::as_str) != Some("catalogue") {
        reply["status"] = json!("unsupported");
        return Some(reply);
    }
    let context = &message["context"];
    let operation = message
        .get("operation")
        .and_then(Value::as_str)
        .unwrap_or("");
    match operation {
        "roots" => {
            reply["data"]["items"] =
                json!([{"id":"samples","kind":"node","title":"Synthetic samples (not live media)"}])
        }
        "browse" => {
            if context.get("nodeRef").and_then(Value::as_str) != Some("samples") {
                reply["status"] = json!("no_match");
            } else {
                let samples = examples();
                match context.get("cursor").and_then(Value::as_str) {
                    None | Some("") => {
                        reply["data"]["items"] = json!(&samples[..2]);
                        reply["data"]["cursor"] = json!("page-2");
                    }
                    Some("page-2") => reply["data"]["items"] = json!(&samples[2..]),
                    _ => {
                        reply["status"] = json!("error");
                        reply["error"] = json!("The fixture page cursor is invalid.");
                    }
                }
            }
        }
        "search" => {
            let query = context.get("query").and_then(Value::as_str).unwrap_or("");
            if query.len() > 512 {
                reply["status"] = json!("error");
            } else {
                let query = query.to_lowercase();
                let found: Vec<_> = examples()
                    .into_iter()
                    .filter(|item| {
                        item["title"]
                            .as_str()
                            .unwrap()
                            .to_lowercase()
                            .contains(&query)
                    })
                    .collect();
                if found.is_empty() {
                    reply["status"] = json!("no_match");
                }
                reply["data"]["items"] = json!(found);
            }
        }
        "suggest" => {
            let limit = context
                .get("limit")
                .and_then(Value::as_u64)
                .unwrap_or(1)
                .min(3) as usize;
            reply["data"]["items"] = json!(&examples()[..limit]);
        }
        "resolve" => {
            reply["status"] = json!("unsupported");
            reply["data"] = json!({});
            reply["error"] = json!("This is a synthetic discovery fixture. It has no live stream or playback endpoint.");
        }
        _ => {
            reply["status"] = json!("unsupported");
            reply["error"] =
                json!("This fixture supports roots, browse, search and suggestions only.");
        }
    }
    Some(reply)
}

#[cfg(target_arch = "wasm32")]
#[link(wasm_import_module = "hakone")]
extern "C" {
    fn host_send(ptr: i32, len: i32) -> i32;
}

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
    if let Ok(message) = serde_json::from_slice(input) {
        if let Some(reply) = handle(&message) {
            if let Ok(bytes) = serde_json::to_vec(&reply) {
                host_send(bytes.as_ptr() as usize as i32, bytes.len() as i32);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn invoke(operation: &str, context: Value) -> Value {
        json!({"type":"invoke","id":"test","contribution":"catalogue","operation":operation,"context":context})
    }
    #[test]
    fn roots_and_pages_have_stable_ids() {
        assert_eq!(
            handle(&invoke("roots", json!({}))).unwrap()["data"]["items"][0]["id"],
            "samples"
        );
        let first = handle(&invoke("browse", json!({"nodeRef":"samples"}))).unwrap();
        assert_eq!(first["data"]["items"].as_array().unwrap().len(), 2);
        let next = handle(&invoke(
            "browse",
            json!({"nodeRef":"samples","cursor":first["data"]["cursor"]}),
        ))
        .unwrap();
        assert_eq!(next["data"]["items"][0]["id"], "sample-podcast");
    }
    #[test]
    fn search_and_suggestions_are_data_only() {
        let found = handle(&invoke("search", json!({"query":"JAZZ"}))).unwrap();
        assert_eq!(found["data"]["items"].as_array().unwrap().len(), 1);
        assert_eq!(found["data"]["items"][0]["id"], "sample-jazz");
        assert_eq!(
            handle(&invoke("suggest", json!({"limit":1}))).unwrap()["data"]["items"]
                .as_array()
                .unwrap()
                .len(),
            1
        );
    }
    #[test]
    fn fixture_never_claims_live_stream_support() {
        let result = handle(&invoke("resolve", json!({"itemRef":"sample-jazz"}))).unwrap();
        assert_eq!(result["status"], "unsupported");
        assert!(result["data"].get("url").is_none());
        assert_eq!(
            handle(&invoke("subscribe", json!({}))).unwrap()["status"],
            "unsupported"
        );
    }
}

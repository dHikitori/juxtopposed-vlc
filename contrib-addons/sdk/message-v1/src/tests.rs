// SPDX-License-Identifier: GPL-2.0-or-later
use super::*;
use serde_json::json;

fn invoke() -> Value {
    json!({"type":"invoke","id":"job-1","contribution":"source","operation":"searchAssets",
        "context":{},"settings":{},"credentials":{"apiKey":{"present":true}},"hostCapabilities":{}})
}
struct Replies(Vec<Value>);
impl Addon for Replies {
    fn handle(&mut self, _: Incoming<'_>) -> Vec<Value> {
        self.0.clone()
    }
}
fn dispatch(replies: Vec<Value>) -> (Result<usize, Error>, usize) {
    let mut guest = Guest::new(Replies(replies));
    let bytes = serde_json::to_vec(&invoke()).unwrap();
    let pointer = guest.buffers.allocate(bytes.len());
    guest
        .buffers
        .allocations
        .get_mut(&pointer)
        .unwrap()
        .copy_from_slice(&bytes);
    let mut sent = 0;
    let result = guest.dispatch(pointer, bytes.len(), |_| {
        sent += 1;
        true
    });
    assert!(guest.buffers.release(pointer, bytes.len()));
    (result, sent)
}
#[test]
fn reads_invoke_presence_and_cancel() {
    let value = invoke();
    match Incoming::parse(&value).unwrap() {
        Incoming::Invoke {
            id, credentials, ..
        } => {
            assert_eq!(id, "job-1");
            assert_eq!(credentials["apiKey"]["present"], true);
        }
        _ => panic!("Expected an invocation."),
    }
    assert!(matches!(
        Incoming::parse(&json!({"type":"cancel","id":"job-1"})),
        Ok(Incoming::Cancel { .. })
    ));
}
#[test]
fn rejects_raw_credentials_and_invalid_identifiers() {
    for credential in [
        json!("secret"),
        json!({"present":true,"value":"secret"}),
        json!({"present":"true"}),
    ] {
        let mut value = invoke();
        value["credentials"]["apiKey"] = credential;
        assert_eq!(Incoming::parse(&value).unwrap_err(), Error::Message);
    }
    for id in ["".to_owned(), "bad\nID".to_owned(), "x".repeat(129)] {
        assert!(Incoming::parse(&json!({"type":"cancel","id":id})).is_err());
    }
}
#[test]
fn accepts_the_hosts_64_credential_status_fields() {
    let mut value = invoke();
    value["credentials"] = json!({});
    for number in 0..64 {
        value["credentials"][format!("key{number}")] = json!({"present":false});
    }
    assert!(Incoming::parse(&value).is_ok());
    value["credentials"]["extra"] = json!({"present":false});
    assert!(Incoming::parse(&value).is_err());
}
#[test]
fn validates_host_result_shape() {
    let mut value = json!({"type":"host-result","id":"call-1","invocation":"job-1","status":"error","error":"Access denied."});
    assert!(matches!(
        Incoming::parse(&value),
        Ok(Incoming::HostResult { ok: false, .. })
    ));
    value["status"] = json!("ok");
    assert!(Incoming::parse(&value).is_err());
    value["response"] = json!({"status":200,"body":"{}"});
    assert!(Incoming::parse(&value).is_ok());
    value["response"] = json!([]);
    assert!(Incoming::parse(&value).is_err());
}
#[test]
fn rejects_pointer_offsets_wrong_lengths_and_double_frees() {
    let mut buffers = Buffers::default();
    let pointer = buffers.allocate(16);
    assert_ne!(pointer, 0);
    assert!(!buffers.release(pointer + 1, 15));
    assert!(!buffers.release(pointer, 15));
    assert!(buffers.release(pointer, 16));
    assert!(!buffers.release(pointer, 16));
    assert_eq!(buffers.allocate(0), 0);
    assert_eq!(buffers.allocate(MAX_MESSAGE + 1), 0);
    let mut guest = Guest::new(Replies(vec![]));
    assert_eq!(guest.dispatch(usize::MAX, 1, |_| true), Err(Error::Buffer));
}
#[test]
fn bounds_outstanding_allocations_and_reuses_capacity() {
    let mut buffers = Buffers::default();
    let mut pointers = Vec::new();
    for _ in 0..MAX_BUFFERS {
        let ptr = buffers.allocate(1);
        assert_ne!(ptr, 0);
        pointers.push(ptr);
    }
    assert_eq!(buffers.allocate(1), 0);
    assert!(buffers.release(pointers[0], 1));
    assert_ne!(buffers.allocate(1), 0);
}
#[test]
fn validates_full_batch_before_host_effects() {
    assert_eq!(
        dispatch(vec![json!({"type":"test"}); MAX_OUTPUTS]),
        (Ok(MAX_OUTPUTS), MAX_OUTPUTS)
    );
    assert_eq!(
        dispatch(vec![json!({}); MAX_OUTPUTS + 1]),
        (Err(Error::OutputLimit), 0)
    );
    assert_eq!(
        dispatch(vec![json!({}), json!(false)]),
        (Err(Error::Message), 0)
    );
    let large = json!({"body":"x".repeat(MAX_MESSAGE / 2)});
    assert_eq!(
        dispatch(vec![large.clone(), large]),
        (Err(Error::OutputLimit), 0)
    );
}
#[test]
fn reserves_the_exact_ipc_envelope_and_separators() {
    let empty_body = serde_json::to_vec(&json!({"body":""})).unwrap().len();
    let body = "x".repeat(MAX_MESSAGE - OUTPUT_ENVELOPE_BYTES - empty_body);
    assert_eq!(dispatch(vec![json!({"body":body})]), (Ok(1), 1));
    assert_eq!(
        dispatch(vec![json!({"body":body + "x"})]),
        (Err(Error::OutputLimit), 0)
    );
    let body = "x".repeat(MAX_MESSAGE - OUTPUT_ENVELOPE_BYTES - empty_body - 3);
    assert_eq!(dispatch(vec![json!({}), json!({"body":body})]), (Ok(2), 2));
}
#[test]
fn binds_pending_calls_and_cancellation_to_invocations() {
    let mut pending = Pending::new(2);
    assert!(pending.insert("call-1", "job-1", 10).is_ok());
    assert_eq!(pending.insert("call-1", "job-2", 11), Err(11));
    assert!(pending.insert("call-2", "job-2", 20).is_ok());
    assert_eq!(pending.insert("call-3", "job-3", 30), Err(30));
    assert_eq!(pending.take("call-1", "job-2"), None);
    assert_eq!(pending.len(), 2);
    pending.cancel("job-1");
    assert_eq!(pending.take("call-1", "job-1"), None);
    assert_eq!(pending.take("call-2", "job-2"), Some(20));
    assert!(pending.is_empty());
}
#[test]
fn reused_pending_id_rejects_a_late_old_invocation_result() {
    let mut pending = Pending::new(2);
    assert!(pending.insert("call-1", "slice-1", 10).is_ok());
    pending.cancel("slice-1");
    assert!(pending.insert("call-1", "slice-2", 20).is_ok());
    assert_eq!(pending.take("call-1", "slice-1"), None);
    assert_eq!(pending.len(), 1);
    assert_eq!(pending.take("call-1", "slice-2"), Some(20));
    assert!(pending.is_empty());
}

#[test]
fn contract_types_follow_the_schema_limits() {
    use crate::contract::*;
    let retention = Retention { reuse_seconds: Some(999_999), keep: Some(Keep::Library), offline_seconds: Some(60) };
    assert_eq!(retention.to_value(), json!({"reuseSeconds": MAX_REUSE_SECONDS, "keep": "library", "offlineSeconds": 60}));
    let message = result("job-1", Status::NoMatch, json!({}));
    assert_eq!(message["status"], "no_match");
    assert_eq!(Status::ALL.len(), 9);
    let mut item = Item::new("station-1", ItemKind::Media, &"x".repeat(600));
    item.available = Some(true);
    let row = item.to_value().unwrap();
    assert_eq!(row["title"].as_str().unwrap().chars().count(), MAX_TITLE);
    assert!(Item::new("bad\nid", ItemKind::Media, "x").to_value().is_none());
    let page = Page { items: vec![item], revision: Some("r1".into()), next_check_seconds: Some(5), ..Page::default() };
    let value = page.to_value().unwrap();
    assert_eq!(value["nextCheckSeconds"], MIN_NEXT_CHECK_SECONDS);
    assert!(Page { revision: Some("r 1".into()), ..Page::default() }.to_value().is_none());
    assert!(Page { cursor: Some(json!("")), ..Page::default() }.to_value().is_none());
}

#[test]
fn a_self_test_message_calls_the_addon_check() {
    struct Checked(bool);
    impl Addon for Checked {
        fn handle(&mut self, _: Incoming<'_>) -> Vec<Value> {
            vec![]
        }
        fn self_test(&mut self) -> bool {
            self.0
        }
    }
    for (passes, status) in [(true, "ok"), (false, "error")] {
        let mut guest = Guest::new(Checked(passes));
        let bytes = serde_json::to_vec(&json!({"type":"self-test","id":"hakone-self-test"})).unwrap();
        let pointer = guest.buffers.allocate(bytes.len());
        guest.buffers.allocations.get_mut(&pointer).unwrap().copy_from_slice(&bytes);
        let mut sent = vec![];
        assert_eq!(guest.dispatch(pointer, bytes.len(), |out| { sent.push(serde_json::from_slice::<Value>(out).unwrap()); true }), Ok(1));
        assert_eq!(sent[0]["status"], status);
        assert_eq!(sent[0]["id"], "hakone-self-test");
    }
}

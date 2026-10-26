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

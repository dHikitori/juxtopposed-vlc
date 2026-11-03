// SPDX-License-Identifier: GPL-2.0-or-later
use hakone_addon_message_v1::{
    serde_json::{json, Value},
    Addon, Incoming, Pending,
};

struct Fixture {
    pending: Pending<()>,
}
impl Addon for Fixture {
    fn handle(&mut self, message: Incoming<'_>) -> Vec<Value> {
        match message {
            Incoming::Invoke { id, operation, .. } if operation == "wait" => {
                let call = format!("call-{id}");
                if self.pending.insert(&call, id, ()).is_err() {
                    return vec![];
                }
                vec![
                    json!({"type":"host-call","id":call,"invocation":id,"operation":"playback.observe","request":{}}),
                ]
            }
            Incoming::Invoke { id, .. } => vec![
                json!({"type":"result","id":id,"status":"match","data":{"pending":self.pending.len()}}),
            ],
            Incoming::HostResult { id, invocation, .. } => {
                if self.pending.take(id, invocation).is_none() {
                    return vec![];
                }
                vec![json!({"type":"result","id":invocation,"status":"match","data":{"ok":true}})]
            }
            Incoming::Cancel { id, .. } => {
                self.pending.cancel(id);
                vec![]
            }
        }
    }
}
hakone_addon_message_v1::export_addon!(
    Fixture,
    Fixture {
        pending: Pending::new(8)
    }
);

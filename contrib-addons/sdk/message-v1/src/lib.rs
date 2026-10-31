// SPDX-License-Identifier: GPL-2.0-or-later
//! Guest messages and the `wasm-message-v1` ABI. The host owns all I/O.
pub use serde_json;
/// Contract types generated from the published schemas.
pub mod contract;
use serde_json::{Map, Value};
use std::collections::BTreeMap;

pub const MAX_MESSAGE: usize = 1024 * 1024;
pub const MAX_OUTPUTS: usize = 64;
const OUTPUT_ENVELOPE_BYTES: usize = br#"{"type":"messages","messages":[]}"#.len();
const MAX_BUFFERS: usize = 32;
const MAX_BUFFER_BYTES: usize = 32 * MAX_MESSAGE;

fn identifier(value: &str) -> bool {
    !value.is_empty() && value.len() <= 128 && !value.chars().any(char::is_control)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Buffer,
    Json,
    Message,
    OutputLimit,
    Host,
}

/// A validated view. Credential data contains presence flags only.
#[derive(Debug, Clone, Copy)]
pub enum Incoming<'a> {
    Invoke {
        id: &'a str,
        contribution: &'a str,
        operation: &'a str,
        context: &'a Map<String, Value>,
        settings: &'a Map<String, Value>,
        credentials: &'a Map<String, Value>,
        capabilities: &'a Map<String, Value>,
        raw: &'a Value,
    },
    HostResult {
        id: &'a str,
        invocation: &'a str,
        ok: bool,
        response: Option<&'a Map<String, Value>>,
        error: &'a str,
        raw: &'a Value,
    },
    Cancel {
        id: &'a str,
        raw: &'a Value,
    },
}
impl<'a> Incoming<'a> {
    pub fn parse(value: &'a Value) -> Result<Self, Error> {
        let object = value.as_object().ok_or(Error::Message)?;
        let id = object
            .get("id")
            .and_then(Value::as_str)
            .filter(|s| identifier(s))
            .ok_or(Error::Message)?;
        match object.get("type").and_then(Value::as_str) {
            Some("invoke") => {
                let field = |key| {
                    object
                        .get(key)
                        .and_then(Value::as_str)
                        .filter(|s| identifier(s))
                        .ok_or(Error::Message)
                };
                let map = |key| {
                    object
                        .get(key)
                        .and_then(Value::as_object)
                        .ok_or(Error::Message)
                };
                let credentials = map("credentials")?;
                if credentials.len() > 64
                    || credentials.iter().any(|(key, status)| {
                        !identifier(key)
                            || status.as_object().is_none_or(|status| {
                                status.len() != 1
                                    || !status.get("present").is_some_and(Value::is_boolean)
                            })
                    })
                {
                    return Err(Error::Message);
                }
                Ok(Self::Invoke {
                    id,
                    contribution: field("contribution")?,
                    operation: field("operation")?,
                    context: map("context")?,
                    settings: map("settings")?,
                    credentials,
                    capabilities: map("hostCapabilities")?,
                    raw: value,
                })
            }
            Some("host-result") => {
                let invocation = object
                    .get("invocation")
                    .and_then(Value::as_str)
                    .filter(|s| identifier(s))
                    .ok_or(Error::Message)?;
                let ok = match object.get("status").and_then(Value::as_str) {
                    Some("ok") => true,
                    Some("error") => false,
                    _ => return Err(Error::Message),
                };
                let response = match object.get("response") {
                    Some(value) => Some(value.as_object().ok_or(Error::Message)?),
                    None => None,
                };
                if ok && response.is_none() {
                    return Err(Error::Message);
                }
                let error = match object.get("error") {
                    Some(value) => value
                        .as_str()
                        .filter(|s| s.len() <= MAX_MESSAGE)
                        .ok_or(Error::Message)?,
                    None => "",
                };
                Ok(Self::HostResult {
                    id,
                    invocation,
                    ok,
                    response,
                    error,
                    raw: value,
                })
            }
            Some("cancel") => Ok(Self::Cancel { id, raw: value }),
            _ => Err(Error::Message),
        }
    }
    pub fn value(self) -> &'a Value {
        match self {
            Self::Invoke { raw, .. } | Self::HostResult { raw, .. } | Self::Cancel { raw, .. } => {
                raw
            }
        }
    }
}

pub trait Addon {
    fn handle(&mut self, message: Incoming<'_>) -> Vec<Value>;
    /// The offline check before install (runtime.selfTest in the manifest).
    /// Run your parsing on built-in sample data; make no host call. The
    /// host refuses a release whose check returns false.
    fn self_test(&mut self) -> bool {
        false
    }
}

/// Exact pointer and length pairs prevent invalid frees or reads in ABI exports.
#[derive(Default)]
pub struct Buffers {
    allocations: BTreeMap<usize, Box<[u8]>>,
    bytes: usize,
}
impl Buffers {
    pub fn allocate(&mut self, length: usize) -> usize {
        if length == 0
            || length > MAX_MESSAGE
            || self.allocations.len() >= MAX_BUFFERS
            || self
                .bytes
                .checked_add(length)
                .is_none_or(|n| n > MAX_BUFFER_BYTES)
        {
            return 0;
        }
        let mut buffer = vec![0_u8; length].into_boxed_slice();
        let pointer = buffer.as_mut_ptr() as usize;
        self.allocations.insert(pointer, buffer);
        self.bytes += length;
        pointer
    }
    pub fn release(&mut self, pointer: usize, length: usize) -> bool {
        if self.input(pointer, length).is_none() {
            return false;
        }
        self.allocations.remove(&pointer);
        self.bytes -= length;
        true
    }
    fn input(&self, pointer: usize, length: usize) -> Option<&[u8]> {
        self.allocations
            .get(&pointer)
            .filter(|buffer| buffer.len() == length)
            .map(AsRef::as_ref)
    }
}

pub struct Guest<A> {
    pub buffers: Buffers,
    addon: A,
}
impl<A: Addon> Guest<A> {
    pub fn new(addon: A) -> Self {
        Self {
            buffers: Buffers::default(),
            addon,
        }
    }
    pub fn dispatch(
        &mut self,
        pointer: usize,
        length: usize,
        mut send: impl FnMut(&[u8]) -> bool,
    ) -> Result<usize, Error> {
        let bytes = self.buffers.input(pointer, length).ok_or(Error::Buffer)?;
        let value: Value = serde_json::from_slice(bytes).map_err(|_| Error::Json)?;
        let messages = if value.get("type").and_then(Value::as_str) == Some("self-test") {
            let id = value
                .get("id")
                .and_then(Value::as_str)
                .filter(|id| identifier(id))
                .ok_or(Error::Message)?;
            let status = if self.addon.self_test() { "ok" } else { "error" };
            vec![serde_json::json!({"type":"result","id":id,"status":status,"data":{}})]
        } else {
            self.addon.handle(Incoming::parse(&value)?)
        };
        if messages.len() > MAX_OUTPUTS {
            return Err(Error::OutputLimit);
        }
        // Validate the full batch before the first host effect.
        let mut size = OUTPUT_ENVELOPE_BYTES;
        let mut encoded = Vec::with_capacity(messages.len());
        for message in messages {
            if !message.is_object() {
                return Err(Error::Message);
            }
            let bytes = serde_json::to_vec(&message).map_err(|_| Error::Json)?;
            let separator = usize::from(!encoded.is_empty());
            size = size
                .checked_add(bytes.len())
                .and_then(|size| size.checked_add(separator))
                .filter(|size| *size <= MAX_MESSAGE)
                .ok_or(Error::OutputLimit)?;
            encoded.push(bytes);
        }
        let count = encoded.len();
        for bytes in encoded {
            if !send(&bytes) {
                return Err(Error::Host);
            }
        }
        Ok(count)
    }
}

/// Pending calls bind a host request ID to its parent invocation.
pub struct Pending<T> {
    entries: BTreeMap<String, (String, T)>,
    limit: usize,
}
impl<T> Pending<T> {
    pub fn new(limit: usize) -> Self {
        Self {
            entries: BTreeMap::new(),
            limit: limit.min(128),
        }
    }
    pub fn insert(&mut self, id: &str, invocation: &str, state: T) -> Result<(), T> {
        if !identifier(id)
            || !identifier(invocation)
            || self.entries.len() >= self.limit
            || self.entries.contains_key(id)
        {
            return Err(state);
        }
        self.entries
            .insert(id.to_owned(), (invocation.to_owned(), state));
        Ok(())
    }
    pub fn take(&mut self, id: &str, invocation: &str) -> Option<T> {
        if !self
            .entries
            .get(id)
            .is_some_and(|entry| entry.0 == invocation)
        {
            return None;
        }
        self.entries.remove(id).map(|entry| entry.1)
    }
    pub fn cancel(&mut self, invocation: &str) {
        self.entries.retain(|_, entry| entry.0 != invocation);
    }
    pub fn len(&self) -> usize {
        self.entries.len()
    }
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

/// Export this macro once in a `cdylib` crate for `wasm32-unknown-unknown`.
#[macro_export]
macro_rules! export_addon {
    ($state:ty, $constructor:expr) => {
        #[cfg(target_arch = "wasm32")]
        mod hakone_message_exports {
            use super::*;
            #[link(wasm_import_module = "hakone")]
            unsafe extern "C" {
                fn host_send(pointer: i32, length: i32) -> i32;
            }
            std::thread_local! {
                static GUEST: std::cell::RefCell<$crate::Guest<$state>> =
                    std::cell::RefCell::new($crate::Guest::new($constructor));
            }
            #[no_mangle]
            pub extern "C" fn hakone_alloc(length: i32) -> i32 {
                if length <= 0 {
                    return 0;
                }
                GUEST.with(|guest| guest.borrow_mut().buffers.allocate(length as usize)) as i32
            }
            #[no_mangle]
            pub extern "C" fn hakone_dealloc(pointer: i32, length: i32) {
                if length <= 0 {
                    return;
                }
                GUEST.with(|guest| {
                    guest
                        .borrow_mut()
                        .buffers
                        .release(pointer as u32 as usize, length as usize);
                });
            }
            #[no_mangle]
            pub extern "C" fn hakone_dispatch(pointer: i32, length: i32) {
                let outcome = GUEST.with(|guest| {
                    guest.borrow_mut().dispatch(
                        pointer as u32 as usize,
                        length as usize,
                        |bytes| unsafe {
                            host_send(bytes.as_ptr() as usize as i32, bytes.len() as i32) == 0
                        },
                    )
                });
                if outcome.is_err() {
                    panic!("Invalid add-on message or output limit.");
                }
            }
        }
    };
}

#[cfg(test)]
mod tests;

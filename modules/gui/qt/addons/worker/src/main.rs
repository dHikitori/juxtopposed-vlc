// SPDX-License-Identifier: GPL-2.0-or-later
//! A deliberately IO-free guest runtime. All guest effects are JSON messages
//! returned to the authenticated parent; there are no WASI imports or secrets.
use wasmtime::error::{bail, Context, Result};
use serde_json::{json, Value};
use std::io::{self, BufRead, Write};
use wasmtime::{Caller, Config, Engine, Linker, Memory, Module, Store, StoreLimits, StoreLimitsBuilder, TypedFunc};

const MAX_MESSAGE: usize = 1024 * 1024;
const MAX_MODULE: usize = 16 * 1024 * 1024;
const MAX_MEMORY: usize = 64 * 1024 * 1024;
const MAX_OUTGOING: usize = 64;
const OUTPUT_ENVELOPE_BYTES: usize = br#"{"type":"messages","messages":[]}"#.len();
const DISPATCH_FUEL: u64 = 10_000_000;

struct State {
    limits: StoreLimits,
    messages: Vec<Value>,
    bytes: usize,
}

struct Runtime {
    store: Store<State>,
    memory: Memory,
    alloc: TypedFunc<i32, i32>,
    dealloc: TypedFunc<(i32, i32), ()>,
    dispatch: TypedFunc<(i32, i32), ()>,
}

impl Runtime {
    fn new(bytes: &[u8]) -> Result<Self> {
        if bytes.len() > MAX_MODULE { bail!("module exceeds size limit"); }
        let mut config = Config::new();
        config.consume_fuel(true);
        config.max_wasm_stack(512 * 1024);
        config.wasm_multi_memory(false);
        // A small reservation also makes worker virtual-memory use predictable.
        config.memory_reservation(MAX_MEMORY as u64);
        let engine = Engine::new(&config)?;
        let module = Module::from_binary(&engine, bytes).context("invalid portable Wasm")?;
        for import in module.imports() {
            if import.module() != "vlc" || import.name() != "host_send" {
                bail!("unsupported module import");
            }
        }
        let state = State {
            limits: StoreLimitsBuilder::new().memory_size(MAX_MEMORY)
                .instances(1).memories(1).tables(1).table_elements(16384)
                .trap_on_grow_failure(true).build(),
            messages: Vec::new(), bytes: OUTPUT_ENVELOPE_BYTES,
        };
        let mut store = Store::new(&engine, state);
        store.limiter(|state| &mut state.limits);
        store.set_fuel(DISPATCH_FUEL)?;
        let mut linker = Linker::new(&engine);
        linker.func_wrap("vlc", "host_send", |mut caller: Caller<'_, State>, ptr: i32, len: i32| -> Result<i32> {
            let len = usize::try_from(len).context("invalid message length")?;
            let separator = usize::from(!caller.data().messages.is_empty());
            if len > MAX_MESSAGE || caller.data().messages.len() >= MAX_OUTGOING {
                bail!("outgoing message quota exceeded");
            }
            let memory = caller.get_export("memory").and_then(|e| e.into_memory())
                .context("guest memory is unavailable")?;
            let start = ptr as u32 as usize;
            let end = start.checked_add(len).context("invalid message range")?;
            let data = memory.data(&caller).get(start..end).context("message outside guest memory")?;
            let message: Value = serde_json::from_slice(data).context("invalid outgoing JSON")?;
            if !message.is_object() { bail!("outgoing message must be an object"); }
            // JSON number normalization can increase the final encoded size.
            let encoded = serde_json::to_vec(&message)?.len();
            if caller.data().bytes.checked_add(encoded).and_then(|n| n.checked_add(separator)).is_none_or(|n| n > MAX_MESSAGE) {
                bail!("outgoing message quota exceeded");
            }
            caller.data_mut().messages.push(message);
            caller.data_mut().bytes += encoded + separator;
            Ok(0)
        })?;
        let instance = linker.instantiate(&mut store, &module).context("cannot instantiate guest")?;
        // Start functions may not emit host calls before an invocation exists.
        if !store.data().messages.is_empty() { bail!("module emitted messages during instantiation"); }
        let memory = instance.get_memory(&mut store, "memory").context("missing guest memory")?;
        let alloc = instance.get_typed_func::<i32, i32>(&mut store, "vlc_alloc")?;
        let dealloc = instance.get_typed_func::<(i32, i32), ()>(&mut store, "vlc_dealloc")?;
        let dispatch = instance.get_typed_func::<(i32, i32), ()>(&mut store, "vlc_dispatch")?;
        Ok(Self { store, memory, alloc, dealloc, dispatch })
    }

    fn dispatch(&mut self, message: &Value) -> Result<Vec<Value>> {
        if !message.is_object() { bail!("dispatch input must be an object"); }
        let bytes = serde_json::to_vec(message)?;
        if bytes.len() > MAX_MESSAGE { bail!("incoming message quota exceeded"); }
        self.store.set_fuel(DISPATCH_FUEL)?;
        self.store.data_mut().messages.clear();
        self.store.data_mut().bytes = OUTPUT_ENVELOPE_BYTES;
        let len = i32::try_from(bytes.len())?;
        let ptr = self.alloc.call(&mut self.store, len).context("guest allocation failed")?;
        self.memory.write(&mut self.store, ptr as u32 as usize, &bytes)
            .context("guest allocation outside memory")?;
        self.dispatch.call(&mut self.store, (ptr, len)).context("guest dispatch failed")?;
        self.dealloc.call(&mut self.store, (ptr, len)).context("guest deallocation failed")?;
        Ok(std::mem::take(&mut self.store.data_mut().messages))
    }
}

// Guest imports remain the primary security boundary. Linux additionally denies
// new file/network/process access after the module has been loaded and compiled.
// Only the inherited parent pipes are used after this point.
#[cfg(target_os = "linux")]
fn restrict_process() -> Result<()> {
    #[cfg(target_arch = "x86_64")]
    const ARCH: u32 = 0xc000003e;
    #[cfg(target_arch = "aarch64")]
    const ARCH: u32 = 0xc00000b7;
    #[cfg(not(any(target_arch = "x86_64", target_arch = "aarch64")))]
    { bail!("worker OS sandbox is not implemented for this Linux architecture"); }
    #[cfg(any(target_arch = "x86_64", target_arch = "aarch64"))]
    {
        fn instruction(code: u16, jt: u8, jf: u8, k: u32) -> libc::sock_filter {
            libc::sock_filter { code, jt, jf, k }
        }
        let mut filter = vec![
            instruction(0x20, 0, 0, 4), // seccomp_data.arch
            instruction(0x15, 1, 0, ARCH),
            instruction(0x06, 0, 0, 0x80000000), // kill wrong architecture
            instruction(0x20, 0, 0, 0), // seccomp_data.nr
        ];
        // Reject socket creation, executable launches and path-based filesystem
        // changes/opens; no guest can obtain an additional IO descriptor.
        let denied = [libc::SYS_socket, libc::SYS_socketpair, libc::SYS_connect,
            libc::SYS_bind, libc::SYS_listen, libc::SYS_accept, libc::SYS_accept4,
            libc::SYS_openat, libc::SYS_execve, libc::SYS_execveat,
            libc::SYS_ptrace, libc::SYS_mount, libc::SYS_unlinkat,
            libc::SYS_renameat, libc::SYS_renameat2, libc::SYS_mkdirat,
            libc::SYS_linkat, libc::SYS_symlinkat, libc::SYS_mknodat];
        for syscall in denied {
            filter.push(instruction(0x15, 0, 1, syscall as u32));
            filter.push(instruction(0x06, 0, 0, 0x00050000 | libc::EPERM as u32));
        }
        #[cfg(target_arch = "x86_64")]
        for syscall in [libc::SYS_open, libc::SYS_creat, libc::SYS_unlink,
            libc::SYS_rename, libc::SYS_mkdir, libc::SYS_link, libc::SYS_symlink] {
            filter.push(instruction(0x15, 0, 1, syscall as u32));
            filter.push(instruction(0x06, 0, 0, 0x00050000 | libc::EPERM as u32));
        }
        filter.push(instruction(0x06, 0, 0, 0x7fff0000)); // allow remaining VM/pipe calls
        let program = libc::sock_fprog { len: filter.len().try_into()?, filter: filter.as_mut_ptr() };
        // SAFETY: the BPF array lives across prctl, which copies it into kernel memory.
        unsafe {
            if libc::prctl(libc::PR_SET_DUMPABLE, 0, 0, 0, 0) != 0
                || libc::prctl(libc::PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0
                || libc::prctl(libc::PR_SET_SECCOMP, 2, &program) != 0 {
                return Err(io::Error::last_os_error()).context("cannot restrict worker");
            }
        }
        Ok(())
    }
}

#[cfg(not(target_os = "linux"))]
fn restrict_process() -> Result<()> {
    // The Wasm import boundary is still enforced. Native host-specific process
    // restrictions are negotiated separately by the parent capability profile.
    Ok(())
}

fn write_message(out: &mut impl Write, message: &Value) -> Result<()> {
    serde_json::to_writer(&mut *out, message)?;
    out.write_all(b"\n")?;
    out.flush()?;
    Ok(())
}

fn run() -> Result<()> {
    let path = std::env::args_os().nth(1).context("expected a portable module path")?;
    let file = std::fs::File::open(path).context("cannot read module")?;
    use std::io::Read;
    let mut bytes = Vec::new();
    file.take((MAX_MODULE + 1) as u64).read_to_end(&mut bytes)?;
    let mut runtime = Runtime::new(&bytes)?;
    // Initialize standard streams before applying the OS restrictions.
    let input = io::stdin();
    let output = io::stdout();
    let mut reader = input.lock();
    let mut out = output.lock();
    restrict_process()?;
    write_message(&mut out, &json!({"type":"ready", "protocol":"wasm-message-v1"}))?;
    loop {
        let mut line = Vec::new();
        let count = reader.by_ref().take((MAX_MESSAGE + 2) as u64).read_until(b'\n', &mut line)?;
        if count == 0 { break; }
        if line.len() > MAX_MESSAGE + 1 || !line.ends_with(b"\n") {
            bail!("invalid IPC frame length");
        }
        let message: Value = serde_json::from_slice(&line).context("invalid IPC JSON")?;
        let messages = runtime.dispatch(&message)?;
        write_message(&mut out, &json!({"type":"messages", "messages":messages}))?;
    }
    Ok(())
}

fn main() {
    if run().is_err() {
        // Never forward guest strings, source URLs, paths, or response bodies to logs.
        let _ = writeln!(io::stderr(), "Add-on worker stopped: invalid module, protocol or execution limit.");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn module(dispatch: &str, imports: &str) -> Vec<u8> {
        wat::parse_str(format!(r#"(module
            (import "vlc" "host_send" (func $send (param i32 i32) (result i32)))
            {imports}
            (memory (export "memory") 1 1024)
            (func (export "vlc_alloc") (param i32) (result i32) i32.const 4096)
            (func (export "vlc_dealloc") (param i32 i32))
            (func (export "vlc_dispatch") (param $ptr i32) (param $len i32) {dispatch})
        )"#)).unwrap()
    }
    #[test]
    fn copies_messages_and_runs_repeated_dispatches() {
        let mut runtime = Runtime::new(&module("local.get $ptr local.get $len call $send drop", "")).unwrap();
        for n in 0..3 {
            let input = json!({"type":"test","n":n});
            assert_eq!(runtime.dispatch(&input).unwrap(), vec![input]);
        }
    }
    #[test]
    fn outgoing_batch_reserves_the_complete_ipc_envelope() {
        fn with_reply(bytes: &[u8]) -> Runtime {
            let encoded: String = bytes.iter().map(|byte| format!("\\{byte:02x}")).collect();
            let wasm = wat::parse_str(format!(r#"(module
                (import "vlc" "host_send" (func $send (param i32 i32) (result i32)))
                (memory (export "memory") 17 1024)
                (data (i32.const 0) "{encoded}")
                (func (export "vlc_alloc") (param i32) (result i32) i32.const 1052672)
                (func (export "vlc_dealloc") (param i32 i32))
                (func (export "vlc_dispatch") (param i32 i32)
                    i32.const 0 i32.const {} call $send drop))"#, bytes.len())).unwrap();
            Runtime::new(&wasm).unwrap()
        }
        let empty = serde_json::to_vec(&json!({"body":""})).unwrap().len();
        for extra in [0, 1] {
            let reply = json!({"body":"x".repeat(MAX_MESSAGE - OUTPUT_ENVELOPE_BYTES - empty + extra)});
            let bytes = serde_json::to_vec(&reply).unwrap();
            let mut runtime = with_reply(&bytes);
            let result = runtime.dispatch(&json!({}));
            if extra == 0 {
                let line = json!({"type":"messages", "messages":result.unwrap()});
                assert_eq!(serde_json::to_vec(&line).unwrap().len(), MAX_MESSAGE);
            } else { assert!(result.is_err()); }
        }
        let empty = r#"{"n":1e9,"body":""}"#;
        let body = "x".repeat(MAX_MESSAGE - OUTPUT_ENVELOPE_BYTES - empty.len());
        let raw = format!(r#"{{"n":1e9,"body":"{body}"}}"#);
        assert_eq!(raw.len() + OUTPUT_ENVELOPE_BYTES, MAX_MESSAGE);
        let mut runtime = with_reply(raw.as_bytes());
        assert!(runtime.dispatch(&json!({})).is_err());
    }
    #[test]
    fn rejects_ambient_wasi() {
        assert!(Runtime::new(&module("", r#"(import "wasi_snapshot_preview1" "random_get" (func))"#)).is_err());
    }
    #[test]
    fn bounds_invalid_guest_pointer() {
        let mut runtime = Runtime::new(&module("i32.const -1 i32.const 20 call $send drop", "")).unwrap();
        assert!(runtime.dispatch(&json!({})).is_err());
    }
    #[test]
    fn interrupts_noncooperative_guest() {
        let mut runtime = Runtime::new(&module("(loop $forever br $forever)", "")).unwrap();
        assert!(runtime.dispatch(&json!({})).is_err());
    }
    #[test]
    fn limits_memory_and_non_object_messages() {
        let too_large = wat::parse_str("(module (memory 1025))").unwrap();
        assert!(Runtime::new(&too_large).is_err());
        let mut runtime = Runtime::new(&module("", "")).unwrap();
        assert!(runtime.dispatch(&json!(false)).is_err());
    }
}

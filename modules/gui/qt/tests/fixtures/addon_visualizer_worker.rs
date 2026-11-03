// SPDX-License-Identifier: GPL-2.0-or-later
// Test fixture. Build with the pinned rustc and no external crates.
#![no_std]
use core::panic::PanicInfo;

#[link(wasm_import_module = "hakone")]
unsafe extern "C" {
    fn host_send(pointer: *const u8, length: usize) -> i32;
}
static mut INPUT: [u8; 1024 * 1024] = [0; 1024 * 1024];
static mut OUTPUT: [u8; 512] = [0; 512];

#[unsafe(no_mangle)]
pub extern "C" fn hakone_alloc(length: usize) -> *mut u8 {
    if length > 1024 * 1024 { core::ptr::null_mut() }
    else { core::ptr::addr_of_mut!(INPUT).cast() }
}
#[unsafe(no_mangle)]
pub extern "C" fn hakone_dealloc(_: *mut u8, _: usize) {}

fn find(input: &[u8], pattern: &[u8]) -> Option<usize> {
    input.windows(pattern.len()).position(|part| part == pattern)
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hakone_dispatch(pointer: *const u8, length: usize) {
    if length > 1024 * 1024 { return; }
    let input = unsafe { core::slice::from_raw_parts(pointer, length) };
    if find(input, b"\"type\":\"invoke\"").is_none() { return; }
    let Some(start) = find(input, b"\"id\":\"") else { return; };
    let start = start + 6;
    let Some(size) = input[start..].iter().position(|byte| *byte == b'"') else { return; };
    if size == 0 || size > 128 { return; }
    let id = &input[start..start + size];
    if !id.iter().all(|byte| byte.is_ascii_alphanumeric() || *byte == b'-') { return; }
    let prefix = b"{\"type\":\"result\",\"id\":\"";
    let suffix = b"\",\"status\":\"match\",\"data\":{\"parameters\":{\"gain\":2}}}";
    let output = core::ptr::addr_of_mut!(OUTPUT).cast::<u8>();
    unsafe {
        core::ptr::copy_nonoverlapping(prefix.as_ptr(), output, prefix.len());
        core::ptr::copy_nonoverlapping(id.as_ptr(), output.add(prefix.len()), id.len());
        core::ptr::copy_nonoverlapping(suffix.as_ptr(), output.add(prefix.len() + id.len()), suffix.len());
        host_send(output.cast_const(), prefix.len() + id.len() + suffix.len());
    }
}
#[panic_handler]
fn panic(_: &PanicInfo) -> ! { loop {} }

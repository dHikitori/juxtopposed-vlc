# Native media helper policy

A contained add-on input must keep the native input policy for all media requests.
The `ytdl` module starts an external process. That process cannot inherit the
native policy. The module must refuse a scoped input before it starts the process.
Child inputs and revoked scopes must also be refused.

The Linux test includes the actual `ytdl` module and replaces only process start.
It proves that an ordinary native input reaches the helper once. Scoped inputs,
child inputs, and revoked scopes do not reach it. The normal test passed on
27 October 2026. This test does not start a real external helper.

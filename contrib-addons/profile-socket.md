# Profile control socket

`StoreProfileEndpoint`, `StoreProfileSocket` and `StoreProfileListener`
supply the local transport for the [profile control
protocol](profile-control-protocol.md). They are host-only. They carry the
fixed 96-byte frames and grant no native authority. The coordinator owner,
client owner, connection-bound write permit and app mount are not
implemented yet. See [the continuation plan](07-continuation-plan.md),
Step 2.

## Endpoint

The first `forProfile()` call for a profile creates
`hakone-authority.endpoint` in the profile: `HAKONEE1` and 32 random bytes,
owner-only. It is written to a temporary file and published without
replacement (`link()` on POSIX, `MoveFileExW` without
`MOVEFILE_REPLACE_EXISTING` on Windows), so concurrent first calls share one
secret. A damaged, linked, group-readable or hard-linked secret is refused
and kept. Whole-profile recovery owns its replacement.

The endpoint name hashes the user identity, the secret and the canonical
profile path. Other users cannot predict it. That prevents pipe-name
squatting on Windows and directory squatting under `/tmp`. Same-user checks
still decide admission; the secret is not a credential.

Unix uses a private directory, in this order: the directory the caller
supplies, `$XDG_RUNTIME_DIR/hakone`, the macOS user temporary directory
(`_CS_DARWIN_USER_TEMP_DIR`) plus `hakone`, then `/tmp/hakone-<secret-derived>`.
A missing or unsafe `$XDG_RUNTIME_DIR` is not created or used. The socket
path must fit `sockaddr_un::sun_path`. Processes with different runtime
environments do not find each other. Their peers stay unavailable and do not
open admission.

## Transport

- Unix: `SO_PEERCRED` (Linux) or `getpeereid` checks the peer user. The
  listener binds the exact path with mode 0600 inside the private directory.
- Windows: a local-only named pipe with an owner-only protected DACL and
  `PIPE_REJECT_REMOTE_CLIENTS`. The client opens it once with identification
  impersonation only, and compares the server process token SID.
- The server checks the client user once, in `accept()`. The client checks
  the server when the connection opens.
- Reads are bounded to 32 frames per batch, with a read buffer of the same
  size. Output above the same bound closes the connection. Received
  callbacks run after the native socket signal returns.
- Loss or destruction calls the native `revoke` callback before the control
  `lost` callback. Callbacks can destroy their socket.
- Only an outgoing connection has a timer: a 5-second single-shot connect
  deadline. Accepted sockets have no handshake deadline yet. The coordinator
  owner adds it (plan §2, note 3).

## Verification

Test target `test_addon_store_profile_socket`, registered in Automake, meson
and CMake. It ran with `test_addon_store_profile_peer` in the six-unit
native producer closure (socket, protocol, generation, process fence, store
authority, native notifications).

| Build | Socket | Peer |
| --- | ---: | ---: |
| Linux normal | 41 | 16 |
| Linux ASAN/UBSAN | 41 | 16 |
| Native Windows (MinGW, Qt 6.8.3 static) | 32 | 16 |

There are no failures, skips or sanitizer reports. Windows compiles out ten
POSIX-only cases and adds one pipe ACL case. The two cross-user cases use Docker to run a second
account. They skip, with a reason, when Docker or its image is not
available.

Four control variants each fail their selected test:

| Control | Change | Failing test |
| --- | --- | --- |
| ignore-user | the POSIX user check accepts everyone | `aDifferentUserIsRefusedEvenWithPermissiveTestPaths` |
| direct-callback | callbacks run inside the native signal | `receivedCallbacksRunAfterTheNativeSignal` |
| unbounded-output | no output bound | `outgoingBackpressureRefusesUnlimitedQueueing` |
| predictable-name | the secret is left out of the name | `theSecretChangesTheName` |

The Windows component link needed `qt_import_plugins(<test> NO_DEFAULT)`:
static Qt Network otherwise imports plugin objects that the contrib build
tree does not keep. `modules/gui/qt/addons/CMakeLists.txt` now links
`advapi32` in lowercase, which the case-sensitive cross linker needs.

These are component checks. They do not establish the peer owners,
cross-process Core Apply, macOS runtime or full app behavior.

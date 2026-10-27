# Git object formats

The host accepts exact semantic-version tags from approved HTTPS repositories. A developer can also select a local repository explicitly. The package source, tag object, commit object, object format, and package digest form the release identity. A package digest uses SHA-256 for both Git object formats.

Before the fetch, the host obtains the exact `refs/tags/<tag>` advertisement with `ls-remote --tags --refs`. The parser accepts one consistent object-ID width: 40 hexadecimal characters for SHA-1 or 64 for SHA-256. A mixed or malformed advertisement fails. The `--refs` option excludes peeled annotation lines. See [Git ls-remote](https://git-scm.com/docs/git-ls-remote).

The host creates a private bare repository in the advertised format. SHA-1 retains the existing initialization command. SHA-256 requires `--object-format=sha256` to succeed. A tool without that support returns a failure. The host does not retry a SHA-256 release with SHA-1. See [Git init](https://git-scm.com/docs/git-init).

The fetched tag must equal the advertised tag object. Commit and blob IDs must use the same format. The host reads objects without a checkout. Existing restrictions on hooks, filters, credential helpers, protocols, redirects, links, submodules, file counts, and package sizes remain in effect.

An update check selects a compatible tag from the installed source. The exact fetch repeats the advertisement and compares its result with the first selection. Install then checks the retained tag and version locks. A format change cannot replace a locked release, even when its package bytes are equal. Preparation does not activate an update or verify its publisher.

All Git commands in one fetch share one monotonic time budget. An update check passes only its remaining budget to the exact fetch. Cancellation and bounded child cleanup remain active. The host clears the output candidate before preparation, so a failed request cannot return an older candidate.

HTTPS certificate verification remains active. The host permits the trusted process environment to supply `GIT_SSL_CAINFO`. A package cannot supply it. Git documents this variable as the certificate-authority file override for `http.sslCAInfo`. See [Git configuration](https://git-scm.com/docs/git-config#Documentation/git-config.txt-httpsslCAInfo).

## Test boundary

The tests use temporary repositories and the installed Git tool. The HTTPS fixture uses the installed smart-HTTP backend, a loopback server, and a test certificate authority. A request without that authority must fail. No test requires an existing public SHA-256 repository or an API key.

The matrix covers SHA-1 and SHA-256; annotated and lightweight tags; HTTPS and developer sources; compatible updates; exact tag selection when a branch has the same name; missing tags; manifest source mismatch; moved tags and changed repository formats between advertisement and fetch; locked release format changes; a tool that refuses SHA-256; and a shared deadline. Parser tests also reject mixed formats across semantic-version and other tag names.

The installed Linux tool used for these tests is Git 2.34.1. The simulated older-tool case proves a clean capability refusal. It does not claim compatibility with every historical Git release. Windows and macOS still need the same wire and tool tests with their installed Git builds.

The final focused run passed 28 Git cases and seven parser cases normally and with AddressSanitizer and UndefinedBehaviorSanitizer. Leak, stack-use-after-return, and floating-point conversion diagnostics were also active. All diagnostic output files were empty. The related package suite passed 69 cases.

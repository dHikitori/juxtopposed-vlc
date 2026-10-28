# Background package list

The Extensions screen must not validate package files on the UI thread.
An actual Windows probe measured one existing `packages()` call at about
421 ms with a local temporary store and 1,598 ms with a WSL network path.
Repeated reads can freeze the screen for several seconds.

`AddonPackageSummaries` supplies a separate display list. The Manager captures
an owned reader in memory. Joined app work then reads and validates package
files and checks the captured credential store. The worker does not access a
Manager QObject. The existing synchronous package API and mutation checks stay
available to their current callers.

The service starts after an explicit view request. It runs one read at a time,
combines newer refresh requests, and refuses old generations or revoked store
authority. Loading clears the old display rows. Refusal has an error state;
it is distinct from a valid empty list. The display has a 256-row limit.

Package actions must call the memory-only `isCurrent()` check when requested
and again when a confirmation is accepted. Cached display rows do not grant
permission to change a package. Global temporary disable, request cancellation,
and prepared package review remain available while the list loads.

Manager retirement and app retirement close the service. Its worker keeps
owned data through the joined app drain. Service destruction does not wait
on a blocked file read and cannot publish a late result.

At this implementation checkpoint, the source peer review is complete.
The focused normal and sanitizer tests and the full Windows rebuild are
pending. The UI hook is a separate change. Actual Windows Extensions-click
responsiveness is not yet verified.

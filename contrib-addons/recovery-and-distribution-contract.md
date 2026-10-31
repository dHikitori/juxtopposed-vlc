# Add-on recovery and app packages

The core Add-ons page owns these controls. An add-on cannot replace them.

## Safe mode

The user can disable add-ons temporarily during a repository request. The control keeps installed packages, settings, saved keys, and private data. It withdraws active work. A pending package review uses the current safe-mode value before it enables a package.

## App package review

The app registers a package directory and an exact package ID through a C++ API. Guest code cannot register that directory. The manager keeps the registered source and version. A registered newer version has a review action in the installed package row.

Review copies the declared package contents into an immutable candidate on counted background work. It checks the ID, source, version, compatibility, and package contents. The installed release stays active until approval. The permission and digest review is the same review that the Git installer uses. Cancel does not change the installed release or the removal policy.

### Startup update prompt

The startup package check already reads each installed app package. For an
installed release older than the app's copy, it also records the installed
version, whether the package is on, and the permissions that the app's copy
asks for and the current grants do not give (`AddonManager.appUpdates`).
After the check, one prompt lists these updates with their version change.
It does no package read of its own and starts no timer or network request.

- **Update** and **Update all** (shown as "Update the others" when some
  remain) install updates that ask for no new permission.
- An update that asks for new permissions lists them and installs only
  through its own **Approve and update**.
- Each install is the same review: the app's copy is staged as an immutable
  candidate and approved; the add-on keeps its on or off state. A failed
  stage or approval shows its reason and can be retried.
- **Later** (or Escape) records the offered versions in the app settings
  (`Redesign/AddonUpdatesDismissed`); the prompt returns for a newer
  version. The Extensions row keeps its "Update to" badge and review.
- The prompt waits while the first-run window is open.

Repository updates are not part of the prompt: finding them needs a network
check, which stays the explicit action in Extensions.

A Git or developer installation of the same ID takes precedence. The app does not replace that installation. Its row does not offer an app update. A removed app package can enter review only through its registered app path.

Approval records the exact distribution digest in the same atomic state pointer as the installed release. The original removal policy remains in its file. Only a valid current distribution snapshot with matching approval can supersede that policy. Rollback, recovery, another digest, a source-class change, or corrupt state cannot reuse approval. Another removal removes that current pointer and exposes the original removal policy again. This avoids a policy-file clear before a failed install.

## Removal and saved data

**Remove add-on** keeps settings, saved keys, and private data. A valid settings archive contains the original manifest and source identity. A later install can restore these settings only for that source class and source. The normal settings migration checks still apply. Damaged package files move intact into the exact package recovery directory.

**Delete add-on and saved data** also deletes the exact package's retained settings, private data, damaged files, and protected credentials. A removed package with retained files has a **Delete saved data** action. These rows grant cleanup authority only. They cannot grant install, update, network, account, or playback authority.

The manager acquires a shared per-package retirement hold before it admits removal work. The hold invalidates old authority tokens and refuses fresh readers for that ID in all managers for the store. The epoch change also invalidates existing snapshots for the store. Other package IDs can obtain a new snapshot. If task admission fails, the hold ends. No saved state or temporary key changes on that refusal. Accepted full removal clears the live key cache only after task admission.

Removal checks the package ID and contained paths before it contacts the credential store. It refuses symbolic-link paths, NTFS junctions, special files, and too many directory entries. It holds the store lock for the saved-state and file mutations. It does not accept a path from QML. Same-process retirement checks use memory only. External filesystem edits remain subject to the profile-trust boundary.

Credential cleanup runs on an owned background store. It uses the exact package attributes on Linux, the exact generic-credential target prefix on Windows, and the exact service plus account prefix on macOS. These names remain internal native storage names. No key value enters the UI. The Linux command has a bounded process wait. Native platform errors remain errors.

If key deletion fails, the package stays disabled and its files remain for retry. The UI reports the failure. It does not report complete cleanup. File deletion is also not a filesystem transaction. A partial file failure remains an error and retains a cleanup action where saved files remain available.

The application lifetime owner counts the removal task through its Qt cleanup and thread join. Closing the manager cancels publication and refuses new work. It does not wait on file or platform I/O on the GUI thread.

## Platform references

The implementation uses the prefix filter and exact-target deletion described in [CredEnumerateW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credenumeratew) and [CredDeleteW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-creddeletew). The macOS search uses attributes from [SecItemCopyMatching](https://developer.apple.com/documentation/security/secitemcopymatching(_:_:)). The path checks use the distinct link and junction queries in [QFileInfo](https://doc.qt.io/qt-6.8/qfileinfo.html). The Linux no-match result follows the [libsecret command implementation](https://github.com/GNOME/libsecret/blob/master/tool/secret-tool.c).

Linux tests use a temporary credential command and test values. They do not read the user's credential store. Windows and macOS need their own native execution proof. Source support does not establish that proof.

## Verification checkpoint

Normal host tests pass 187 cases across Packages (54), Runtime (27), StoreAuthority (6), NativeAuthority (9), Broker (70), and Storage (21). The runtime run uses the exact built worker and has no skip. The three QML suites pass 59 cases. Focused sanitizers pass 87 cases across Packages, Runtime, and StoreAuthority, with no diagnostic output. These sanitizer targets instrument Manager, PackageStore, CredentialStore, StoreAuthority, and their tests; the remaining host archive and Qt libraries use their normal build.

The quota test holds two admitted background jobs. A third removal request must fail without changing saved state or forgetting its temporary key. Other cases cover a blocked platform deletion, fresh readers in a second manager, source and ID changes during app review, immutable reviewed bytes, rollback and recovery, damaged state, unrelated files, and cleanup after ordinary removal. The UI cases cover confirmation, cancel, stale state, safe mode during work, and the existing permission review.

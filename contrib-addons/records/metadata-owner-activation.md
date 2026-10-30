# Native metadata owner activation (Step 4)

Step 4 of the [continuation plan](../07-continuation-plan.md). Written
30 October 2026. Linux (WSL2), meson debug build `build-redesign`.

## The setting

`metadata-native-owner` is a Core option (`src/libvlc-module.c`), off by
default: "Use add-on metadata providers". With it off, the app is as before.
No owner is created and Core runs its Lua art scripts.

With it on, MainUI creates `AddonNativeMetadataOwner` right after the
metadata service and registers it as Core's metadata owner for this instance.
From then on:

- Core's legacy lease refuses, so no Lua art script runs from the preparser
  or fetcher (`src/preparser/metadata_owner.c`).
- Every preparse with metadata (player, playlist, library) creates its anchor
  job on the service's backend (`src/preparser/metadata_pipeline.c`). Local
  readers (tags, sidecars, attachments, folder art) still run in Core.
- The switch is one-way for the instance. When the owner closes at app
  retirement, Core does not go back to the Lua scripts.

The owner's image cache is `<add-on profile>/native-artwork-cache`. It locks
each entry with `QLockFile`, so windows that share the profile can share it.

## Families: parity, then no Core script

`contrib-addons/sdk/artwork-providers/tests/test_parity.py`
(`addon_artwork_parity`, label `metadata`) runs each Core Lua art script
and its add-on on the same media and the same service answers. The Lua
script runs in a Lua 5.4 interpreter with a stand-in `vlc` table, and the
add-on runs in the real worker. The test is skipped without Lua 5.4.

| Family | Cases where both choose the same artwork, outcome and search |
| --- | --- |
| MusicBrainz (with Amazon) | Release ID with Cover Art Archive; release ID with only an ASIN; artist and album search; artist and title search; no results; radio listing |
| Last.fm | Album page; only the default image; no album; TV listing |
| Google Images | Album cover; TV logo; no results; nothing to search |
| French TV | All 27 channels, each also with " HD" and " (bas débit)"; unknown and missing channel |

The add-ons use HTTPS and current service addresses. The test compares
search text and the chosen image, not hosts. These are the known
differences, and the test checks each one:

| Case | Lua script | Add-on |
| --- | --- | --- |
| Google page with only scripts | unavailable | no match |
| Last.fm page without an image | unavailable | no match |
| Album "Album (Deluxe) CD2" | Searches `Album%20%28Deluxe%29%20`: it cleans the name after encoding, so the bracket rule never matches | Searches `Album` |
| French TV "France Info" | Old 800-pixel address | Repaired 500-pixel address |

All four families pass. With the owner on, the app therefore offers no
built-in member: `Options::nativeFamilies` is empty, so the owner registers
none of the `hakone.builtin.*` artwork providers, and the family plan chooses
each family's add-on. `aReplacedFamilyOffersOnlyItsAddon` covers the option.
With the owner off, the Lua scripts stay, because they are the default path.
They can go when the default changes.

Episode names and subtitles are Qt-side. In the app they already go through
the shared metadata service whenever it exists: the TVMaze add-on, and the
OpenSubtitles adapter in the shared owner. Their older clients run only
without a service.

## Old and new remote paths never run at the same time

The Core tests stay in the `metadata` label:

- `test_src_misc_metadata_owner`: `pending_legacy_cannot_begin_after_owner_install`, `running_legacy_blocks_dispatch_until_native_end`
- `test_src_misc_metadata_fetcher`: `owner_install_interrupts_running_legacy_before_owned_dispatch`, `the_real_executor_drains_legacy_io_before_owned_dispatch`
- `test_src_misc_metadata_preparser_internal`: `owner_or_consent_transition_discards_only_the_legacy_copy`
- `test_src_preparser_owned_pipeline`

## Full app

The test driver's `metadata` command reports whether Core's owner is this
window's service, whether the owner is active, and how many requests Core
sent it. `addon_app_smoke.py --owner` turns the setting on and checks these
in every window.

| Run | Result |
| --- | --- |
| Owner off, driver | No owner; 0 requests |
| Owner on, one window (`addon_app_smoke_owner`) | Owner is this window's service; playback sent 1 request through it |
| Owner on, two windows (`addon_app_smoke_owner_two_windows`), both quit orders | Each window owns its own process's Core; install, discovery and quit pass |

Network access stays off in these runs, so no lookup leaves the machine.
The request is the player's preparse. The media library preparses through
the same Core preparser (`modules/misc/medialibrary/LazyPreparser.h`), but
the smoke test adds no library folder, so that route is not driven in the
app yet.

## Still open

- The default stays off. Changing it needs the live checks in Step 5:
  Windows, the user's GUI run, and "Subtitle53". That check is not defined
  in this repository; the user runs it.
- A live online lookup through the owner with network access on (the plan's
  public probes ran per provider on 27 October; see
  [the provider migration](metadata-provider-migration.md)).
- Contract gates that are not part of activation: exhaustive not-found
  status, identity choice and persistence, user-cleared fields, cache
  retention ([owner contract](../native-metadata-owner-contract.md)).
- HTTP and LAN embedded-artwork scope stays native.

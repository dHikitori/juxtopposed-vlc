# Native artwork approval

This document specifies a cutover gate. The native approval record is implemented and passes its normal C suite. Its consumer mounts remain open work.

The core exports `vlc_artwork_item_IsRestricted`, `vlc_artwork_item_GetURI`, `vlc_artwork_item_SetApprovedURI`, and `vlc_artwork_uri_IsLocalFile`. The getter and local-URI check perform no image or network operation. The setter accepts a local-file URI after trusted caller validation. It does not decode or validate image bytes itself.

Native metadata Apply now transfers an approved result record to its original target. It allocates the proof before admission, then writes the field and its reserved revision with that proof under the same item lock. It performs no allocation after the first committed field. A text-only file URI fails the complete patch. An unapproved raw field on a restricted item counts as a missing display asset; a current approved record and ordinary native artwork remain protected.

Seven native proof groups pass normally and with ASAN/UBSAN. They include valid transfer, raw fields, stale result records, foreign source/policy, protected user artwork, cancellation, and native clear. The token-limit suite also changes raw and approved artwork after proof preparation; the complete patch is refused without partial writes. The actual Qt broker/service/Apply test remains under app validation.

## Source scope

An input item is restricted when it has a private input policy. A revoked policy does not remove this restriction.

An item with a `vlc-addon-source:` URI is also restricted before the native source bootstrap attaches its policy. The URI comparison must ignore ASCII letter case. An imported or serialized source item must not cause an early image load.

Ordinary native media keeps its existing embedded artwork behavior. Its native remote asset adapter must obey the user's network preference. Add-on assets use the separate public HTTPS policy.

## Private record

The item owner holds one approved artwork URI, its artwork field revision, the source URI, and a held policy reference. Only trusted native code can create this record. Metadata tags, extra fields, text options, package data, and serialized items cannot create it.

The approved URI must identify a local file. The host must validate that file before approval. The native setter writes the URI and captures its new field revision under the item lock. It must refuse an exhausted revision token.

An approved record permits use only when its exact URI and revision equal the current artwork field. Its source URI and policy must still match; the held policy must not be revoked. A same-value write, an intervening write, or a metadata merge must not restore old approval.

Seven normal C groups pass: ordinary native URI reads, raw imported source URIs, approved native copies, field/source changes, policy changes and revocation, metadata/options transfer attempts, and local URI limits. The URI cases include incorrect UTF-8, correct native UTF-8 filenames, remote authorities, control bytes, and malformed escapes. These cases establish the record's behavior. They do not establish the unmounted consumers' behavior.

A native item copy can retain approval. The copy operation must validate the source record under the source item lock. It must bind the copied record to the destination field's new revision. A metadata merge alone cannot transfer approval.

## Consumers

Restricted items can expose only approved local artwork to the Qt image loader, native image widgets, and library artwork consumers. Apply the same rule to `Player.artwork` and `PlayerController::decodeArtURL`.

The legacy downloader must refuse an unapproved embedded artwork URI from a restricted item. It must not fetch that URI with ordinary native HTTP, LAN, or file access. A future scoped asset adapter can fetch public HTTPS artwork under its own source authority. An absent adapter means refusal.

The host can approve a local file after a broker download, a trusted cache lookup, an attachment extraction, or an explicit user file choice. A metadata Save action alone does not approve the old URI.

## Required tests

- Ordinary native media retains its permitted artwork behavior.
- A raw source URI is restricted before policy attachment.
- Restricted items refuse embedded HTTP, LAN, and local file URIs.
- A revoked policy and its copies remain restricted.
- Text metadata cannot create approval.
- An approved local file is usable by the restricted item and a validated copy.
- A same-value write and an ABA write revoke approval.
- A metadata merge or serialization cannot transfer approval.
- The Qt player and local artwork consumers do not expose a refused URI.

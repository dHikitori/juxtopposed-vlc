# Native artwork service stage

The shared metadata service accepts the private native operation
`materializeNativeArtwork`. This operation requires the artwork feature, one
artwork field, an automatic request, and a native item. It is not a guest
operation. Its original native image address never enters provider context.

`AddonNativeArtworkAssets` reads the original native snapshot under the user's
network preference. It preserves the ordinary native HTTP and LAN scope. It
refuses scoped items. Add-on artwork continues through its own asset broker.

The result is a normalized local file with a native approval record. Completion
does not write the target. The native pipeline retains its `StartLocal` parent
anchor through child completion and final guarded Apply. The anchor remains
visible to cancellation after its result is complete.

Withdrawing automatic network access first clears the service's memory consent
bit, then calls `vlc_metadata_backend_CancelAutomatic`. This includes native
anchors which never entered the Qt dispatch map. Manual metadata jobs retain
their existing behavior. A held backend survives a reentrant terminal callback.

Native artwork cancellation takes its interrupt callback under the service
state lock and calls it after unlock. It does not wait for a GUI event. A cancel
which wins before callback registration immediately cancels the late work.

Direct manual native consumers must retain the adapter's `Permit` and use it
for their callback-free Apply. A completed child alone is not final authority.

## Verification

The focused service suite passes 19 cases, with no failed or skipped case.
Address, undefined-behavior, leak, and stack-use-after-return checks also pass
without diagnostics. The tests cover approved native publication, same-value
target edits, consent withdrawal after child completion, completed native
anchors, preserved manual anchors, and interrupted I/O without GUI delivery.
The existing guest artwork and shared native service cases also pass.

The fixture uses synthetic image bytes and no live service. It compiles the
service, coordinator, and native materializer from current source, with the
committed native decoder and the configured Core library. Its recipe and
filtered logs are in `/tmp/hakone-native-artwork-service/`.

## Open production gates

The native metadata owner remains inactive in MainUI. Keep each native provider
until its shared-route consumer replacement passes. Before the materializer is
mounted, include its Qt-using workers in the application shutdown barrier and
retain its native object through native I/O completion.

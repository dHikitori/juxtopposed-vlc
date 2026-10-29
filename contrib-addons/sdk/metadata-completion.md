# Metadata completion

This guide defines the complete `no_match` path for `metadata@1`. The provider
keeps the normal message protocol. Only the host can create a completion receipt.
The host supports strict `no_match` receipts and positive completion receipts
for `match` and `partial`. A positive receipt binds the completed result object;
the consumer still must accept its identities and fields. Native aggregate,
image approval and final metadata writes have separate authority gates.

## NoMatch result

Return `no_match` only after the requested lookup is complete and has no result.
For example:

```json
{"type":"result","id":"request-id","status":"no_match","data":{}}
```

A provider can make zero host calls. A finite local table can return NoMatch.
The host does not require a network request as evidence that the provider ran.
The receipt still needs an actual dispatch for the exact current invocation.
A cached result or a result from another invocation cannot supply this receipt.

The data object can contain only `identity`, `records`, `candidates`, and
`fields`. The arrays must be empty. The fields object must be empty. The provider
can omit identity. If identity is present, it must equal the request identity
after strict host normalization. External ID namespace and scope remain distinct.
Episode numbering must use one scheme without mixed fields. Conflicts, a page
cursor, nonempty data, an error, or a malformed value prevent a receipt.

Do not turn a failed or pending host call into NoMatch. A later successful retry
does not erase the failed call. The host tracks every call for this invocation.
Every call must complete under its actual authority. Canceled, denied, unavailable,
failed, timed-out, malformed, and incomplete calls prevent a receipt.

A guest `complete` flag or claim object cannot create host evidence. A guest
`unsupported` result cannot create an applicability receipt.

## Positive completion

A `match` or `partial` result can supply a positive receipt only for an actual
current invocation with complete host observations and no error. The data must
be a nonempty bounded object. The host binds a digest of its compact JSON
encoding to the exact invocation and context. It can compare the same object
before consumer acceptance. Native-plan preparation publishes the receipt only
after a clean worker exit.

The receipt does not establish that the returned identity or fields are
acceptable. The shared consumer must check them before image resolution or a
metadata write. A positive receipt cannot certify NoMatch. An image failure
can permit a later provider but cannot become a negative lookup result.

The current portable artwork aggregate keeps the slot reserved during consumer
acceptance and image work. It can transfer native Store watches and admission
into Core after the caller has an approved local image. No receipt grants that
image approval. See [the host transfer contract](../artwork-positive-host-transfer.md).

## HTTP status policy

An HTTP response must have a complete body. The host keeps the actual status and
transport outcome. A normal complete 2xx response can qualify, except status 206.
A truncated body, a failed transfer, or a canceled request cannot qualify.

A prefix request can qualify when the body ends cleanly below `responseBytes`.
This also applies when `allowTruncated` is true. A body at the cap cannot qualify,
even if the returned `truncated` flag is false. A body without a declared length
needs a clean end of stream. A pending connection is not complete.

By default, HTTP 404 and HTTP 410 cannot support this receipt. A provider can
declare these statuses only when they mean no result for its exact feature and
operation. Use the optional `completionPolicy` on that contribution:

```json
{
  "version": 1,
  "httpAbsent": [
    {"feature": "video.details", "operation": "enrich", "statuses": [404, 410]}
  ]
}
```

See the [contribution example](examples/metadata-completion-contribution.json).
This is a contribution fragment. A complete package must also declare its runtime,
files, and required permissions. The example does not contact a real service.
Existing reference package manifests remain unchanged.

The policy must have version 1 and one to 16 rules. Each rule has exactly
`feature`, `operation`, and `statuses`. The feature and operation must occur in
that same contribution's declarations. Each pair can occur once. Statuses can
contain 404, 410, or both, without duplicates. Other statuses are not permitted.
The policy applies only to `metadata.provider` with contract `metadata@1`.

The host captures this policy from the installed contribution. A guest reply
cannot change it. A declared 404 or 410 still needs complete host transport
observations. Authentication failures, rate limits, server errors, and failed
response bodies cannot become NoMatch through this declaration.

## Schema and validation scope

The [policy schema](metadata-completion-policy.schema.json) follows Draft 2020-12.
It validates the `completionPolicy` value. It is not a complete manifest schema.
The native manifest validator also enforces contribution type and contract,
feature and operation membership, unique pairs, and native string limits.

The schema cannot compare a rule with another manifest field. It cannot enforce
unique feature/operation pairs when the status arrays differ. Use the native
package validator for those rules. A valid schema result does not authorize a
provider operation.

Use the existing SDK schema tool environment with Python 3.10 or later and
`jsonschema==4.23.0`:

```sh
python3 contrib-addons/sdk/test-metadata-completion-schema.py
```

## Host receipt scope

The host binds a receipt to the exact private native subject, selected slot,
contribution, feature, operation, release, settings revision, account, permissions,
requested fields, and projected context. The host can consume it once while all
captured authority remains current. Cancellation, configuration changes, and app
retirement revoke that authority.

A receipt certifies one executed slot. It does not prove that all providers ran.
The native plan must retain all required authority and account for unavailable
providers before it can use an aggregate result. The final native field and
revision admission remains required. A NoMatch receipt alone cannot write the
artwork status or metadata.

The earlier copied host fixture passed 74 completion cases and 79 shared identity cases
in normal and sanitizer modes. It includes a real Rust worker, the committed
French-TV Wasm table, adversarial native protocol frames, and actual local Qt TLS
responses. This evidence does not prove a live service, the final native
aggregate, or a mounted owner path. The current combined build passes all 200 completion, identity, catalogue, and
store-authority cases after the explicit worker and Wasm path change.

The current receipt and artwork aggregate group passes 86 and 58 cases,
respectively, both normally and with sanitizers. It verifies portable positive
source transfer through final Core Apply. The serial Service runner, native
positive path and mounted owner remain separate integration gates. See
[the current evidence](../artwork-positive-host-transfer.md).

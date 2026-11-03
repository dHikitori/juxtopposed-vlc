# Metadata completion

This guide defines the complete `no_match` path for `metadata@1`. The provider
keeps the normal message protocol. Only the host can create a completion receipt.
The host supports strict `no_match` receipts and positive completion receipts
for `match` and `partial`. A positive receipt binds the completed result object;
the app still checks its identities and fields before it uses them.

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

The data object can contain only `identity`, `records`, `candidates`,
`fields`, and a correct `retention` record (see below). The arrays must be empty. The fields object must be empty. The provider
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

`test-metadata-completion-schema.py` checks the schema against example
policies. It needs Python 3.10 or later and `jsonschema` 4.23.0.

## Receipt scope

A receipt applies to one invocation of one provider, for the same media,
fields, release, settings, account and permissions. A cancellation or a
configuration change makes it invalid. A NoMatch receipt from one provider does
not mean that other providers have no result.

## Retention

Any metadata result can state what the provider permits the host to keep, in
`data.retention`:

```json
{"retention": {"reuseSeconds": 3600, "keep": "library", "offlineSeconds": 86400}}
```

| Key | Values | Without the key |
| --- | --- | --- |
| `reuseSeconds` | Integer, 0 to 86400. How long the host can reuse the answer for the same request, account and settings. 0 means do not reuse. | 600 for `match` and `partial`; 60 for `no_match` and `ambiguous` |
| `keep` | `"memory"` or `"library"`. Use `"library"` only when the data license permits the host to save accepted fields in the media library. | `"memory"`: fields are not saved |
| `offlineSeconds` | Integer, 0 to 604800. After the reuse time ends, how long the host can show the answer, labeled as not current, while the provider is offline. | 0: never shown offline |

The host clamps reuse to 24 hours for `match` and `partial`, 1 hour for
`no_match` and 10 minutes for `ambiguous`. `keep` and `offlineSeconds` apply
only to `match` and `partial`. Errors, timeouts, sign-in requests and rate
limits are never reused as answers. A record with an unknown key or a value
that is not correct permits nothing: no reuse, no saving and no offline use.
The host holds reused answers in memory only. A new account, release or
settings generation does not see them.

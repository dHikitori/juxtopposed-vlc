# Radio Browser

This add-on reads station records from [Radio Browser](https://www.radio-browser.info/). The [primary API reference](https://docs.radio-browser.info/) defines the station UUID, HTTPS search filter, page parameters, and station lookup.

The add-on uses read operations. It sends no station edits, votes, or click updates. The host supplies the `Hakone-Addon/1.0` user agent. API requests use only the declared HTTPS origin.

The source repository URL in the manifest is a proposed location. Repository availability has not been verified.

The official server list returned only `de1.api.radio-browser.info` on 2026-10-28. A new API origin needs a package update and permission review.

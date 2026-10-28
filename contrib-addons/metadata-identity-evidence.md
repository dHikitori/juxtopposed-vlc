# Metadata identity evidence

The coordinator uses one identity boundary for identify, enrichment and asset
search. It projects declared SDK fields into host identity fields. It removes
private caller properties before it sends the provider context.

Known external IDs keep their entity scope. A movie ID can identify a movie.
A series ID can identify a series. An episode ID can identify an episode
without a season or episode number. A parent series ID also needs matching
episode numbering or a matching publication date before it identifies an
episode. MusicBrainz track, album and artist IDs identify their own entity
types. A conflicting supplied ID or other identity fact refuses the match.

A podcast episode needs its own evidence. A matching GUID needs a matching
feed or series. Matching episode numbering or publication date needs a
matching title and feed or series. A title and artist alone do not identify
a podcast episode. Feed-level podcast and title-and-author audiobook matches
remain available.

The boundary retains unknown scoped IDs. It does not treat an unknown ID as
strong evidence. It refuses conflicting host and external ID values, invalid
entity kinds, invalid ID, alias or numbering list or map shapes, and values
that exceed host limits. The identity itself must be a map.
It accepts the host's empty fields and `-1` numeric sentinel as absent facts.

Structured episode numbering has exactly the keys for its declared scheme.
Seasonal, absolute and air-date schemes cannot carry another scheme's keys.
An alias or structured value cannot contradict a populated host value. Host
identity fields can retain valid parallel numbering evidence. Normalization
does not discard that evidence or replace a populated fact with a sentinel.

Invalid request identity ends before provider invocation. An invalid provider
identity cannot become an empty manual-search identity. Invalid result records
can fall through to the next provider. Results retain the normalized identity,
so an ordinary enrichment response and an asset response use the same rules.

The current coordinator passes 79 cases and the identity matcher passes 178
cases in both normal and focused sanitizer builds. There are no failures or
skips. These checks compile the live sources in an isolated Qt test group.
They do not rebuild the app, native Service or Core.

This change supplies evidence checks. It does not supply the trusted identity
dialog, persistent accepted identity or accepted revision. Identify offers
still require a title. A selected weak identity still needs separate native
selection authority before it can pass the final native transaction.

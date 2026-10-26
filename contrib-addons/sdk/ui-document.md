# UI document contract

This page describes the `ui@1` document that the current host can render. The [architecture proposal](../04-ui-themes-and-configuration.md) includes future components. Use only the components on this page for the current host.

The package supplies JSON data. The host supplies the controls, layout, theme, focus behavior, and text display. The host does not execute package QML or JavaScript. Text uses plain text. Text cannot load HTML content.

## Document

```json
{
  "contract": "ui@1",
  "id": "station-list",
  "root": {
    "type": "Page",
    "title": {"message": "stations.title"},
    "children": [
      {
        "type": "List",
        "items": {"bind": "state.stations"},
        "keyField": "ref",
        "emptyText": {"message": "stations.empty"},
        "itemTemplate": {
          "type": "MediaCard",
          "title": {"bind": "item.title"},
          "subtitle": {"bind": "item.description"},
          "artwork": {"bind": "item.artworkHandle"},
          "primaryAction": {
            "command": "org.example.radio/play",
            "arguments": {"item": {"bind": "item.ref"}}
          }
        }
      }
    ]
  }
}
```

The document permits only `contract`, `id`, and `root`. An identifier starts with a letter or an underscore. Its remaining characters can be letters, digits, underscores, full stops, or hyphens. Its maximum length is 96 characters. The names `__proto__`, `prototype`, and `constructor` are not permitted.

## Components

Each component accepts `type`, `id`, `visible`, and `enabled`. The table gives the additional properties.

| Component | Additional properties | Host behavior |
| --- | --- | --- |
| `Page` | `title`, `children` | Column with a title |
| `Section` | `title`, `children` | Column with a title and section spacing |
| `Column` | `children` | Vertical content |
| `Row` | `children` | Horizontal content that can continue on another line |
| `Heading` | `text` | Heading text |
| `Text` | `text` | Body text |
| `Notice` | `text` | Text with an alert role for a screen reader |
| `EmptyState` | `text` | Text for an empty result |
| `Button` | `text`, `icon`, `intent`, `action` | Button with a text label |
| `IconButton` | `text`, `icon`, `action` | Icon button; `text` supplies its accessible name |
| `Image` | `artwork`, `text` | Image from a host asset handle; `text` supplies its accessible name |
| `MediaCard` | `title`, `subtitle`, `artwork`, `primaryAction` | Media title, description, artwork, and optional action |
| `List` | `items`, `keyField`, `itemTemplate`, `emptyText` | Virtual list with a scroll bar |
| `Progress` | `value`, `text` | Progress value from zero to one and a text label |

`Button.intent` can be `primary`, `secondary`, or `destructive`. This value changes the style. It does not give command authority.

The permitted icon names are `play`, `pause`, `stop`, `queue`, `music`, `video`, `network`, `folder`, `playlist`, `refresh`, `search`, `info`, `close`, `settings`, `subtitles`, `favorite`, `home`, `chevron-left`, and `chevron-right`.

The current host does not implement `Table`, `Tabs`, or `FormField` in a UI document. Use the separate generated settings form for package configuration.

## Values and bindings

A text property accepts a string, `{"message":"message.key"}`, or `{"bind":"state.path"}`. A message refers to the host locale dictionary for that package. If the dictionary has no value, the host displays the key.

A binding is one property path. It starts with `state.` or `item.`. Each path part uses the identifier rules above. `state` is the view state. `item` is the current list item. A binding has no expression, function call, index expression, or access to core objects.

`visible` and `enabled` accept a Boolean or a binding that returns a Boolean. An absent property uses `true`. A specified binding with no value uses `false`.

`Progress.value` accepts a number or a binding that returns a finite number. The host limits the displayed value to zero through one.

`List.items` must be a state or item binding. The result must be an array. Each item must have the `keyField` property. Each key must be a string or number, and each key must be different. A list cannot contain another list.

An artwork value is a host asset handle. A package URL or file path does not become an image source. The host must resolve the handle through its asset service.

## Commands

An action has a `command` and optional `arguments`. The command must be in the host command registry for the package. Its identifier has the form `package.id/command-id`. Arguments can contain JSON data and the value bindings above.

The renderer emits a command request. The host then does the authority, argument schema, and user action checks. A button or media card does not give a package playback, queue, library, or network access.

Each command argument object or array can have a maximum of 32 entries. Arguments can have a maximum of eight nested levels.

## Limits and failure behavior

The UI document has a maximum of 256 components, ten nested component levels, and 64 children in each container. State arrays have a maximum of 1,000 items. The JSON data limit is 10,000 values, 20 nested levels, 256 properties per object, 16,384 UTF-16 code units per string, and 131,072 UTF-16 code units across strings.

The renderer does a check of the document and each state projection. An incorrect state update keeps the last correct state. It also disables commands until a correct update arrives. The renderer combines frequent state updates at intervals of 34 milliseconds.

These limits apply to this renderer. The package manifest, message transport, route registry, asset service, and command service have separate limits and authority rules.

## View sessions

The host creates a separate session for each mounted view. Route and modal input must agree with the contribution's `parameters` schema. The host supplies this input in `state.parameters`. It supplies the permitted context in `state.context`.

The context can contain `surface`, `search`, and `layout`. Media identity fields need a separate `metadata.context` grant. The host removes private media URLs, file paths, and core objects. A view does not inherit the context of another view.

A command in a view can return a `state` object or a `patch` object in its result data. `state` replaces the package state. `patch` replaces the specified properties at the first level. The contribution must declare `stateSchema` for these updates. The host keeps `parameters` and `context` under its control. Do not declare these two properties in the package state schema.

The host permits only commands from the same package in a view session. A new command cancels the previous pending command in that session. The host also cancels pending work when the view suspends, closes, or loses its contribution. A result from canceled work cannot change a later session.

For a worker command, the contribution's `command` value names the operation. The contribution must also include this name in `operations`. A route command uses `route` with a route identifier in the same package. A modal command can use `command` with a modal identifier in the same package.

The `parameters`, `stateSchema`, and `resultSchema` fields use a fixed schema subset. Permitted types are `object`, `array`, `string`, `number`, `integer`, and `boolean`. Objects need `properties` and `additionalProperties: false`. Optional keywords are `required`, `enum`, `minimum`, `maximum`, `minLength`, `maxLength`, `minItems`, and `maxItems`. Arrays need an `items` schema. Unknown keywords cause package validation to fail. The schema can have a maximum of ten nested levels and 256 schema nodes.

## Modal results

A modal contribution can declare `resultSchema`. The host supplies Confirm and Cancel outside the package view. A package can put a draft result in `state.result`. Confirm returns this result only if it agrees with `resultSchema`. Cancel returns no result. A modal with no result schema can return only an empty object.

A generic modal result does not change a queue, playlist, or track assignment. The working tree has separate [typed adapters](typed-ui-selections.md) for subtitle and playlist dialogs. They accept only current offered host references. The built-in bodies remain available through core recovery controls. A playlist recovery regression remains open.

The implementation is in [AddonUi.js](../../modules/gui/qt/player/qml/redesign/AddonUi.js), [RedesignAddonView.qml](../../modules/gui/qt/player/qml/redesign/RedesignAddonView.qml), and [RedesignAddonNode.qml](../../modules/gui/qt/player/qml/redesign/RedesignAddonNode.qml). The [UI tests](../../modules/gui/qt/tests/tst_AddonUi.qml) cover the contract and the protected settings form.

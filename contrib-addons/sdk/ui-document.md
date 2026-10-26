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

The implementation is in [AddonUi.js](../../modules/gui/qt/player/qml/redesign/AddonUi.js), [RedesignAddonView.qml](../../modules/gui/qt/player/qml/redesign/RedesignAddonView.qml), and [RedesignAddonNode.qml](../../modules/gui/qt/player/qml/redesign/RedesignAddonNode.qml). The [UI tests](../../modules/gui/qt/tests/tst_AddonUi.qml) cover the contract and the protected settings form.

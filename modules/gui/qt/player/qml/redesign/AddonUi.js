// SPDX-License-Identifier: GPL-2.0-or-later
.pragma library

// This is a data-only contract. Never compile package expressions or component names.
var forbiddenKeys = ["__proto__", "prototype", "constructor"]
var nodeTypes = ["Page", "Section", "Column", "Row", "Heading", "Text", "Notice", "EmptyState", "Button", "IconButton", "Image", "MediaCard", "List", "Progress", "Tabs", "Menu", "MediaRow"]
var icons = ["play", "pause", "stop", "queue", "music", "video", "network", "folder", "playlist", "refresh", "search", "info", "close", "settings", "subtitles", "favorite", "home", "chevron-left", "chevron-right",
    "episodes", "chapters", "more", "check", "replay", "play-outline"]

function own(object, key) {
    return object !== null && typeof object === "object" && Object.prototype.hasOwnProperty.call(object, key)
}
function record(value) { return value !== null && typeof value === "object" && !Array.isArray(value) }
// A key is text: a regular expression test would accept a number, an array
// or undefined as its string form.
function safeKey(key) { return typeof key === "string" && /^[A-Za-z_][A-Za-z0-9_.-]{0,95}$/.test(key) && !forbiddenKeys.includes(key) }
function clone(value) { return JSON.parse(JSON.stringify(value)) }
// A list from C++ arrives as a sequence, for which Array.isArray is false.
// Validate and keep plain copies of host data instead.
function plain(value, fallback) { return value === undefined || value === null ? fallback : clone(value) }

function dataError(value, depth, budget) {
    if (depth > 20 || --budget.nodes < 0) return "The data is larger than the limit."
    if (value === null || typeof value === "boolean") return ""
    if (typeof value === "number") return Number.isFinite(value) ? "" : "A number is not correct."
    if (typeof value === "string") {
        budget.text -= value.length
        return value.length <= 16384 && budget.text >= 0 ? "" : "The text is longer than the limit."
    }
    if (Array.isArray(value)) {
        if (value.length > 1000) return "A list has too many entries."
        for (var i = 0; i < value.length; ++i) {
            var error = dataError(value[i], depth + 1, budget)
            if (error) return error
        }
        return ""
    }
    if (!record(value)) return "Use JSON data."
    var keys = Object.keys(value)
    if (keys.length > 256) return "An object has too many properties."
    for (var k = 0; k < keys.length; ++k) {
        if (forbiddenKeys.includes(keys[k]) || keys[k].length > 128) return "A property name is not permitted."
        var problem = dataError(value[keys[k]], depth + 1, budget)
        if (problem) return problem
    }
    return ""
}
function validateData(value) { return dataError(value, 0, {nodes: 10000, text: 131072}) }
function bindingPath(path) {
    if (typeof path !== "string" || path.length > 256) return false
    var parts = path.split(".")
    return ["state", "item"].includes(parts[0]) && parts.length > 1 && parts.every(part => safeKey(part))
}
function resolve(value, state, item, messages) {
    if (!record(value)) return value
    if (own(value, "message")) return own(messages || {}, value.message) ? String(messages[value.message]) : value.message
    if (!own(value, "bind") || !bindingPath(value.bind)) return undefined
    var parts = value.bind.split(".")
    var result = parts[0] === "state" ? state : item
    for (var i = 1; i < parts.length; ++i) {
        if (!own(result, parts[i])) return undefined
        result = result[parts[i]]
    }
    return result
}
function valueError(value, kind) {
    if (record(value)) {
        var keys = Object.keys(value)
        if (keys.length !== 1) return "A binding is not correct."
        if (own(value, "bind")) return bindingPath(value.bind) ? "" : "A binding path is not permitted."
        if (own(value, "message") && kind === "text" && typeof value.message === "string" && value.message.length <= 128) return ""
        return "Use a declared state binding or a message."
    }
    if (kind === "text") return typeof value === "string" && value.length <= 16384 ? "" : "Use a string that is within the text limit."
    if (kind === "bool") return typeof value === "boolean" ? "" : "A visibility value or an availability value is not correct."
    if (kind === "number") return typeof value === "number" && Number.isFinite(value) ? "" : "A progress value is not correct."
    return ""
}
function registered(id, commands) {
    return typeof id === "string" && /^[a-z][a-z0-9.-]{1,127}\/[A-Za-z0-9_.-]{1,96}$/.test(id)
        && Array.isArray(commands) && commands.some(command => typeof command === "string" ? command === id : record(command) && command.id === id)
}
function argumentError(value, depth) {
    if (depth > 8) return "The command arguments have too many levels."
    if (record(value) && (own(value, "bind") || own(value, "message"))) return valueError(value, "text")
    if (Array.isArray(value)) {
        if (value.length > 32) return "The command has too many arguments."
        for (var i = 0; i < value.length; ++i) { var e = argumentError(value[i], depth + 1); if (e) return e }
    } else if (record(value)) {
        if (Object.keys(value).length > 32) return "The command has too many arguments."
        for (var key of Object.keys(value)) {
            if (!safeKey(key)) return "A command argument name is not correct."
            var error = argumentError(value[key], depth + 1); if (error) return error
        }
    }
    return ""
}
function actionError(action, commands) {
    if (!record(action) || Object.keys(action).some(key => !["command", "arguments"].includes(key)) || !registered(action.command, commands)) return "This command is not available."
    return action.arguments === undefined ? "" : argumentError(action.arguments, 0)
}
function resolveArguments(value, state, item, messages) {
    if (record(value) && (own(value, "bind") || own(value, "message"))) return resolve(value, state, item, messages)
    if (Array.isArray(value)) return value.map(entry => resolveArguments(entry, state, item, messages))
    if (!record(value)) return value
    // A binding to a missing value leaves its argument out, like a missing
    // text binding shows nothing; an undefined value would arrive as null.
    var result = {}
    for (var key of Object.keys(value)) {
        var resolved = resolveArguments(value[key], state, item, messages)
        if (resolved !== undefined) result[key] = resolved
    }
    return result
}
function nodeError(node, commands, depth, budget, insideList) {
    if (--budget.nodes < 0 || depth > 10) return "The view is larger than the limit."
    if (!record(node) || !nodeTypes.includes(node.type)) return "The app cannot use this component type."
    var common = ["type", "id", "visible", "enabled"]
    var fields = {
        Page: ["title", "children"], Section: ["title", "children"], Column: ["children"], Row: ["children"],
        Heading: ["text"], Text: ["text"], Notice: ["text"], EmptyState: ["text"],
        Button: ["text", "icon", "intent", "action"], IconButton: ["text", "icon", "action"],
        Image: ["artwork", "text"], MediaCard: ["title", "subtitle", "artwork", "primaryAction"],
        List: ["items", "keyField", "itemTemplate", "emptyText", "scroll", "more", "moreAction", "moreText"], Progress: ["value", "text"],
        Tabs: ["items", "selected", "text", "action"], Menu: ["text", "icon", "entries"],
        MediaRow: ["title", "subtitle", "number", "mark", "artwork", "progress", "expanded", "primaryAction", "children"]
    }
    for (var key of Object.keys(node)) {
        if (!common.includes(key) && !fields[node.type].includes(key)) return "The app cannot use this component property."
        if (["text", "title", "subtitle", "emptyText", "artwork", "number", "mark", "moreText"].includes(key)) { var textError = valueError(node[key], "text"); if (textError) return textError }
        if (["visible", "enabled", "expanded", "more"].includes(key)) { var boolError = valueError(node[key], "bool"); if (boolError) return boolError }
    }
    if (node.id !== undefined && !safeKey(node.id)) return "A component ID is not correct."
    if (node.icon !== undefined && !icons.includes(node.icon)) return "The app cannot use this icon."
    if (node.intent !== undefined && !["primary", "secondary", "destructive"].includes(node.intent)) return "The app cannot use this button style."
    for (var actionKey of ["action", "primaryAction", "moreAction"]) {
        if (node[actionKey] !== undefined) { var actionProblem = actionError(node[actionKey], commands); if (actionProblem) return actionProblem }
    }
    if (["Button", "IconButton"].includes(node.type) && (!node.text || !node.action)) return "A button must have a label and a command."
    if (node.type === "MediaCard" && !node.title) return "A media card must have a title."
    if (node.type === "Progress" && node.value !== undefined) { var numberError = valueError(node.value, "number"); if (numberError) return numberError }
    if (node.type === "MediaRow") {
        if (!node.title) return "A media row must have a title."
        if (node.progress !== undefined) { var rowProgress = valueError(node.progress, "number"); if (rowProgress) return rowProgress }
    }
    if (node.type === "Tabs") {
        // The items are {key, label} from state. In the action, item.tab is
        // the chosen entry and item is the enclosing list item, if any.
        if (!record(node.items) || !bindingPath(node.items.bind) || Object.keys(node.items).length !== 1) return "Tabs must have a state binding."
        if (!node.text || !node.action) return "Tabs must have a label and a command."
        if (node.selected !== undefined && !(typeof node.selected === "number" && Number.isFinite(node.selected))) { var selectedError = valueError(node.selected, "text"); if (selectedError) return selectedError }
    }
    if (node.type === "Menu") {
        if (!node.text || !Array.isArray(node.entries) || node.entries.length < 1 || node.entries.length > 16) return "A menu must have a label and 1 to 16 entries."
        for (var entry of node.entries) {
            if (!record(entry) || Object.keys(entry).some(key => !["text", "icon", "action", "visible"].includes(key)) || !entry.text || !entry.action) return "A menu entry must have a label and a command."
            var entryText = valueError(entry.text, "text"); if (entryText) return entryText
            if (entry.visible !== undefined) { var entryVisible = valueError(entry.visible, "bool"); if (entryVisible) return entryVisible }
            if (entry.icon !== undefined && !icons.includes(entry.icon)) return "The app cannot use this icon."
            var entryAction = actionError(entry.action, commands); if (entryAction) return entryAction
        }
    }
    if (node.type === "List" && node.scroll !== undefined && typeof node.scroll !== "boolean") return "The list scroll value is not correct."
    if (node.children !== undefined) {
        if (!Array.isArray(node.children) || node.children.length > 64) return "The child list is not correct."
        for (var child of node.children) { var childError = nodeError(child, commands, depth + 1, budget, insideList); if (childError) return childError }
    }
    if (node.type === "List") {
        if (insideList) return "Do not put a list inside another list."
        if (!record(node.items) || !bindingPath(node.items.bind) || Object.keys(node.items).length !== 1 || !safeKey(node.keyField)) return "A list must have a state binding and a unique key field."
        return nodeError(node.itemTemplate, commands, depth + 1, budget, true)
    }
    return ""
}
function validateView(document, commands) {
    var error = validateData(document)
    if (error) return error
    if (!record(document) || document.contract !== "ui@1" || !safeKey(document.id)
            || Object.keys(document).some(key => !["contract", "id", "root"].includes(key))) return "The view contract is not correct."
    return nodeError(document.root, commands, 0, {nodes: 256}, false)
}
function validateProjection(node, state, item, messages) {
    for (var key of ["text", "title", "subtitle", "emptyText", "artwork"]) {
        if (node[key] !== undefined) {
            var value = resolve(node[key], state, item, messages)
            if (value !== undefined && typeof value !== "string") return "A text binding returned a value that is not correct."
        }
    }
    if (["Button", "IconButton", "MediaCard", "MediaRow", "Tabs", "Menu"].includes(node.type)) {
        var label = resolve(["MediaCard", "MediaRow"].includes(node.type) ? node.title : node.text, state, item, messages)
        if (typeof label !== "string" || !label.trim()) return "A control must have a label for the screen reader."
    }
    for (var textKey of ["number", "mark", "moreText"]) {
        if (node[textKey] !== undefined) {
            var textValue = resolve(node[textKey], state, item, messages)
            if (textValue !== undefined && typeof textValue !== "string") return "A text binding returned a value that is not correct."
        }
    }
    for (var boolKey of ["expanded", "more"]) {
        if (node[boolKey] !== undefined) {
            var boolValue = resolve(node[boolKey], state, item, messages)
            if (boolValue !== undefined && typeof boolValue !== "boolean") return "An availability binding returned a value that is not correct."
        }
    }
    if (node.type === "MediaRow" && node.progress !== undefined) {
        var rowValue = resolve(node.progress, state, item, messages)
        if (rowValue !== undefined && (typeof rowValue !== "number" || !Number.isFinite(rowValue))) return "A progress binding returned a value that is not correct."
    }
    if (node.type === "Tabs") {
        var tabs = resolve(node.items, state, item, messages)
        if (tabs !== undefined) {
            if (!Array.isArray(tabs) || tabs.length > 64) return "A tab binding returned a value that is not correct."
            var tabKeys = new Set()
            for (var tab of tabs) {
                if (!record(tab) || !["string", "number"].includes(typeof tab.key) || typeof tab.label !== "string" || !tab.label.trim()) return "Each tab must have a key and a label."
                if (tabKeys.has(typeof tab.key + ":" + tab.key)) return "Each tab must have a different key."
                tabKeys.add(typeof tab.key + ":" + tab.key)
            }
        }
        var chosen = resolve(node.selected, state, item, messages)
        if (chosen !== undefined && !["string", "number"].includes(typeof chosen)) return "A tab binding returned a value that is not correct."
    }
    if (node.type === "Menu") {
        for (var menuEntry of node.entries) {
            var menuLabel = resolve(menuEntry.text, state, item, messages)
            if (typeof menuLabel !== "string" || !menuLabel.trim()) return "A control must have a label for the screen reader."
            var menuVisible = menuEntry.visible === undefined ? true : resolve(menuEntry.visible, state, item, messages)
            if (menuVisible !== undefined && typeof menuVisible !== "boolean") return "An availability binding returned a value that is not correct."
        }
    }
    for (var flag of ["visible", "enabled"]) {
        if (node[flag] !== undefined) {
            var flagValue = resolve(node[flag], state, item, messages)
            if (flagValue !== undefined && typeof flagValue !== "boolean") return "An availability binding returned a value that is not correct."
        }
    }
    if (node.type === "Progress") {
        var progress = resolve(node.value, state, item, messages)
        if (progress !== undefined && (typeof progress !== "number" || !Number.isFinite(progress))) return "A progress binding returned a value that is not correct."
    }
    if (node.type === "List") {
        var items = resolve(node.items, state, item, messages)
        if (items === undefined) return ""
        if (!Array.isArray(items) || items.length > 1000) return "A list binding returned a value that is not correct."
        var keys = new Set()
        for (var entry of items) {
            if (!own(entry, node.keyField) || !["string", "number"].includes(typeof entry[node.keyField])) return "A list item must have a unique key."
            var entryKey = typeof entry[node.keyField] + ":" + entry[node.keyField]
            if (keys.has(entryKey)) return "Each list item must have a different key."
            keys.add(entryKey)
            var entryError = validateProjection(node.itemTemplate, state, entry, messages); if (entryError) return entryError
        }
    }
    for (var child of node.children || []) { var childError = validateProjection(child, state, item, messages); if (childError) return childError }
    return ""
}

function fieldSecret(field) { return field["x-hakone-secret"] === true || field["x-hakone-control"] === "secret" || field.writeOnly === true || field.format === "password" }
function validateSettingsSchema(schema) {
    var error = validateData(schema)
    if (error) return error
    if (!record(schema) || schema.type !== "object" || !record(schema.properties)
            || Object.keys(schema.properties).length > 64 || schema.additionalProperties !== false) return "The app cannot use this settings schema."
    var topKeys = ["$schema", "type", "properties", "required", "additionalProperties", "title", "description"]
    if (Object.keys(schema).some(key => !topKeys.includes(key))) return "The app cannot use this schema keyword."
    if (["$schema", "title", "description"].some(key => schema[key] !== undefined && typeof schema[key] !== "string")) return "A settings label is not correct."
    if (schema.required !== undefined && (!Array.isArray(schema.required) || schema.required.some(key => typeof key !== "string" || !own(schema.properties, key)))) return "The required settings list is not correct."
    var fieldKeys = ["writeOnly", "pattern", "type", "title", "description", "default", "enum", "minimum", "maximum", "minLength", "maxLength", "minItems", "maxItems", "uniqueItems", "items", "format", "x-hakone-control", "x-hakone-label", "x-hakone-help", "x-hakone-secret", "x-hakone-visibleWhen", "x-hakone-restartRequired", "x-hakone-account"]
    for (var name of Object.keys(schema.properties)) {
        var field = schema.properties[name]
        if (!safeKey(name) || !record(field) || !["boolean", "string", "integer", "number", "array"].includes(field.type)) return "The app cannot use this settings field type."
        if (Object.keys(field).some(key => !fieldKeys.includes(key))) return "The app cannot use this field keyword."
        if (field["x-hakone-control"] !== undefined && !["secret", "language-list", "multi-enum", "color", "url", "file", "folder", "account", "duration"].includes(field["x-hakone-control"])) return "The app cannot use this settings editor."
        for (var flag of ["writeOnly", "x-hakone-secret", "x-hakone-restartRequired", "uniqueItems"]) {
            if (field[flag] !== undefined && typeof field[flag] !== "boolean") return "A settings flag is not correct."
        }
        for (var label of ["title", "description", "x-hakone-label", "x-hakone-help"]) {
            if (field[label] !== undefined && typeof field[label] !== "string") return "A settings label is not correct."
        }
        if (field.pattern !== undefined && field.pattern !== "^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$") return "The app cannot use this settings pattern."
        if (field.format !== undefined && !["password", "uri", "color", "language"].includes(field.format)) return "The app cannot use this settings format."
        if (fieldSecret(field) && field.type !== "string") return "Use the protected editor for a secret field."
        if ((field["x-hakone-control"] === "account") !== (field["x-hakone-account"] !== undefined) || field["x-hakone-control"] === "account" && (!fieldSecret(field) || !record(field["x-hakone-account"]))) return "The app cannot use this account field."
        if (fieldSecret(field) && field.default !== undefined) return "Do not set a default value for a secret field."
        if (field.type === "array" && (!record(field.items) || field.items.type !== "string" || Object.keys(field.items).some(key => !["type", "enum"].includes(key)))) return "Use a string list that is within the size limit."
        if (field.type !== "array" && (field.items !== undefined || field.minItems !== undefined || field.maxItems !== undefined)) return "Only a list can have list limits."
        // Choices and the default value have the field's own type.
        const typed = value => field.type === "boolean" ? typeof value === "boolean" : field.type === "string" ? typeof value === "string"
            : field.type === "integer" ? Number.isInteger(value) : field.type === "number" ? typeof value === "number" && Number.isFinite(value)
            : Array.isArray(value) && value.every(entry => typeof entry === "string")
        if (field.enum !== undefined && Array.isArray(field.enum) && field.enum.some(value => !typed(value))) return "The setting choices are not correct."
        if (field.default !== undefined && (!typed(field.default) || Array.isArray(field.enum) && !field.enum.includes(field.default))) return "The default setting is not correct."
        if (field.enum !== undefined && (!Array.isArray(field.enum) || !field.enum.length || field.enum.length > 64 || field.enum.some(value => !["string", "number", "boolean"].includes(typeof value)))) return "The setting choices are not correct."
        for (var limit of ["minimum", "maximum", "minLength", "maxLength", "minItems", "maxItems"]) {
            if (field[limit] !== undefined && (typeof field[limit] !== "number" || !Number.isFinite(field[limit]))) return "A setting limit is not correct."
        }
        for (var lengthLimit of ["minLength", "maxLength", "minItems", "maxItems"]) {
            if (field[lengthLimit] !== undefined && (!Number.isInteger(field[lengthLimit]) || field[lengthLimit] < 0)) return "A length limit is not correct."
        }
        if (field.minLength !== undefined && field.maxLength !== undefined && field.minLength > field.maxLength
                || field.minItems !== undefined && field.maxItems !== undefined && field.minItems > field.maxItems) return "A length range is not correct."
        if (field.type === "array" && field.items.enum !== undefined && (!Array.isArray(field.items.enum) || !field.items.enum.length
                || field.items.enum.length > 64 || field.items.enum.some(value => typeof value !== "string"))) return "The list choices are not correct."
        if (field.minimum !== undefined && field.maximum !== undefined && field.minimum > field.maximum) return "A setting range is not correct."
        if (field["x-hakone-visibleWhen"] !== undefined) {
            var condition = field["x-hakone-visibleWhen"]
            if (!record(condition) || typeof condition.field !== "string" || !own(schema.properties, condition.field) || fieldSecret(schema.properties[condition.field]) || !own(condition, "equals") || Object.keys(condition).some(key => !["field", "equals"].includes(key))) return "A settings condition is not correct."
        }
    }
    return ""
}
function defaults(schema, values) {
    var result = {}
    for (var name of Object.keys(schema.properties || {})) {
        var field = schema.properties[name]
        if (fieldSecret(field)) continue
        if (own(values || {}, name)) result[name] = clone(values[name])
        else if (field.default !== undefined) result[name] = clone(field.default)
    }
    return result
}
function fieldError(field, value, required) {
    if (value === undefined || value === null) return required ? "Enter the required value." : ""
    if (field.type === "boolean" && typeof value !== "boolean") return "Select On or Off."
    if (["integer", "number"].includes(field.type)) {
        if (typeof value !== "number" || !Number.isFinite(value) || field.type === "integer" && !Number.isInteger(value)) return "Enter a correct number."
        if (field.minimum !== undefined && value < field.minimum || field.maximum !== undefined && value > field.maximum) return "Enter a number that is within the permitted range."
    }
    if (field.type === "string") {
        if (typeof value !== "string") return "Enter correct text."
        if (value.length > Math.min(4096, field.maxLength === undefined ? 4096 : field.maxLength) || field.minLength !== undefined && value.length < field.minLength || required && !value.length) return "Enter text that is within the permitted length."
        // Only fixed host validators run in QML; package patterns are never
        // compiled as JavaScript regular expressions.
        if ((field.format === "language" || field.pattern === "^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$") && !/^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$/.test(value)) return "Enter a correct language tag."
        if (field["x-hakone-control"] === "color" && !/^#[0-9a-fA-F]{6}$/.test(value)) return "Enter a color such as #224488."
        if ((field.format === "uri" || field["x-hakone-control"] === "url") && value && !/^https?:\/\/[^\s/]+(?:[/?#][^\s]*)?$/.test(value)) return "Enter an HTTP or HTTPS URL."
    }
    if (field.type === "array") {
        if (!Array.isArray(value) || value.length > Math.min(64, field.maxItems === undefined ? 64 : field.maxItems)
                || field.minItems !== undefined && value.length < field.minItems || value.some(item => typeof item !== "string" || item.length > 256)) return "The list is not within the permitted limits."
        if (field.uniqueItems && new Set(value).size !== value.length) return "Do not repeat a value in this list."
        if (field.items.enum && value.some(item => !field.items.enum.includes(item))) return "Select values from the list."
    }
    if (field.enum && !field.enum.includes(value)) return "Select a value from the list."
    return ""
}
function settingsErrors(schema, values, secrets, operations, credentialPersistenceAvailable) {
    var errors = {}
    var required = schema.required || []
    for (var name of Object.keys(schema.properties || {})) {
        var field = schema.properties[name]
        if (fieldSecret(field)) {
            var operation = operations[name] || {operation: "keep"}
            if (!["keep", "replace", "delete"].includes(operation.operation)) errors[name] = "Select a correct credential action."
            else if (operation.operation === "replace") {
                if (fieldError(field, operation.value, true)) errors[name] = "Enter a correct replacement credential."
                else if (credentialPersistenceAvailable === false && operation.sessionOnly !== true) errors[name] = "Select storage for this session. The secure credential store is not available."
            } else if (operation.operation === "keep" && required.includes(name) && !(secrets[name] && secrets[name].present))
                errors[name] = field["x-hakone-control"] === "account" ? "Connect the account." : "Enter a credential, or keep the saved credential."
        } else {
            var error = fieldError(field, values[name], required.includes(name))
            if (error) errors[name] = error
        }
    }
    return errors
}

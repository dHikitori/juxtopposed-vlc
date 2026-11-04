// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import VLC.MainInterface
import VLC.Dialogs

RedesignShortcutEditor {
    id: root
    property string pendingName: ""
    property string pendingShortcut: ""
    signal saved()
    Binding {
        target: MainCtx
        property: "useGlobalShortcuts"
        value: false
        when: root.visible
        restoreMode: Binding.RestoreBindingOrValue
    }
    RedesignPreferences { id: draft; ctx: MainCtx }
    shortcuts: draft.shortcutCatalog()
    dirty: draft.dirty
    errorMessage: draft.errorMessage
    values: {
        draft.revision
        const values = {}
        for (const row of shortcuts) {
            values[row.name] = draft.value(row.name)
            if (row.globalName) values[row.globalName] = draft.value(row.globalName)
        }
        return values
    }
    onAboutToShow: draft.discard()
    onCancelRequested: { draft.discard(); close() }
    onSaveRequested: if (draft.save()) { root.saved(); close() }
    onClearRequested: name => draft.assignShortcut(name, "")
    onCaptureRequested: (name, key, modifiers) => {
        const shortcut = draft.shortcutForKey(key, modifiers)
        if (!shortcut.length) return
        const conflicts = draft.shortcutConflicts(name, shortcut)
        if (conflicts.length) {
            pendingName = name; pendingShortcut = shortcut
            conflictDialog.message = qsTr("Hakone uses %1 for %2. Use %1 for this action?").arg(shortcut).arg(conflicts.map(conflict => {
                const row = shortcuts.find(row => row.name === conflict.name || row.globalName === conflict.name)
                return row ? row.title : conflict.title
            }).join(", "))
            conflictDialog.open()
        } else draft.assignShortcut(name, shortcut)
    }
    RedesignConfirmationDialog {
        id: conflictDialog
        objectName: "shortcutConflictDialog"
        parent: root.contentItem
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        title: qsTr("Shortcut already in use")
        confirmText: qsTr("Use shortcut")
        videoBackdropContent: Component {
            RedesignSurfaceBackdrop {
                sourceItem: root.background ? root.background.parent : null
                scaleFactor: root.scaleFactor
                nativeVideo: false
            }
        }
        onAccepted: draft.assignShortcut(root.pendingName, root.pendingShortcut, true)
    }
}

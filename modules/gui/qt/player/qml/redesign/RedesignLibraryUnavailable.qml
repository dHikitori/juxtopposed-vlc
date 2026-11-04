// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

RedesignLibraryView {
    id: root
    category: 0
    readonly property var categoryTitles: [qsTr("Home"), qsTr("Videos"), qsTr("Music"), qsTr("Network"), qsTr("Playlists"), qsTr("Recent files")]
    heading: categoryTitles[category]
    breadcrumbs: [{label: heading, kind: "category"}]
    homeSectionsVisible: false
    canEnqueue: false
    sortFields: []
    showAddRow: category > 0 && category < 5
    addLabel: category === 3 ? qsTr("Open network stream") : qsTr("Open files")
    emptyText: qsTr("The media library is unavailable. Check Library settings and restart Hakone to use your library. You can still open files, folders or network streams.")

    function resetLocation(newCategory) {
        category = newCategory
        searchText = ""
    }
    onCategoryRequested: newCategory => resetLocation(newCategory)
    onBreadcrumbActivated: resetLocation(category)
}

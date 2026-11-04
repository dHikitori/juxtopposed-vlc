// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// A media row: a click selects it, a double-click activates it.
// Only these rows may handle doubleClicked: once it is connected, the button
// drops the clicked of a double-click's second press. On a menu button that
// loses a quick click after the click that closed the menu.
RedesignButton {
    mediaItem: true
    onDoubleClicked: if (mediaItem && !singleClickActivate) activated()
}

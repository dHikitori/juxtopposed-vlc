// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "AddonTheme.js" as Contract

// A bounded snapshot for host adapters/preview. Applying it to the entire core
// interface remains the responsibility of the host theme service.
QtObject {
    property var document: null
    property bool lightTheme: false
    property string userAccent: ""
    property bool highContrast: false
    property bool reducedTransparency: false
    readonly property string errorMessage: document === null ? "" : Contract.validate(document)
    readonly property bool valid: errorMessage.length === 0
    readonly property bool usesFallbackVariant: document !== null && valid && !document.variants[lightTheme ? "light" : "dark"]
    readonly property var snapshot: Contract.resolve(document, lightTheme, userAccent, {highContrast: highContrast, reducedTransparency: reducedTransparency})
}

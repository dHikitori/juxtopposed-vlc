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
    // Validate and resolve one plain JSON copy of host data (a C++ list is a
    // sequence, not an Array). JSON.stringify would drop a function silently,
    // so a document with one cannot be copied, and is refused.
    readonly property var plainDocument: {
        if (document === null) return null
        try {
            return JSON.parse(JSON.stringify(document, (key, value) => {
                if (typeof value === "function") throw new TypeError("executable theme property")
                return value
            }))
        } catch (error) { return undefined }
    }
    readonly property string errorMessage: document === null ? ""
        : plainDocument === undefined || plainDocument === null ? qsTr("The app cannot read this theme.")
        : Contract.validate(plainDocument)
    readonly property bool valid: errorMessage.length === 0
    readonly property bool usesFallbackVariant: document !== null && valid && !plainDocument.variants[lightTheme ? "light" : "dark"]
    readonly property var snapshot: Contract.resolve(valid ? plainDocument : null, lightTheme, userAccent, {highContrast: highContrast, reducedTransparency: reducedTransparency})
}

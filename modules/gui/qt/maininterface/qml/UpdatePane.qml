/*****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Templates as T

import VLC.MainInterface
import VLC.Widgets as Widgets
import VLC.Style
import VLC.Util

T.Pane {
    id: root

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    horizontalPadding: VLCStyle.margin_large
    verticalPadding: VLCStyle.margin_small

    property bool animations: true

    signal dismissRequested()

    readonly property ColorContext colorContext: ColorContext {
        id: theme
        colorSet: ColorContext.Window // ###

        focused: root.activeFocus
        hovered: root.hovered
        enabled: root.enabled
    }

    background: Rectangle {
        color: theme.bg.primary
    }

    contentItem: Column {
        spacing: VLCStyle.margin_xsmall

        RowLayout {
            id: rowLayout

            anchors.left: parent.left
            anchors.right: parent.right

            spacing: VLCStyle.margin_small

            readonly property bool collapseText: (width < VLCStyle.colWidth(5))

            Image {
                id: cone

                sourceSize: Qt.size(VLCStyle.icon_normal, VLCStyle.icon_normal)
                source: SVGColorImage.colorize("qrc:///misc/hakone.svg").accent(theme.accent).uri()
            }

            Widgets.LabelExt {
                id: titleLabel

                Layout.fillWidth: true

                text: {
                    switch (UpdateModel.updateStatus) {
                        case UpdateModel.NeedUpdate:
                            return qsTr("A new version is available: %1 %2").arg(UpdateModel.productName).arg(UpdateModel.version)
                        case UpdateModel.UpToDate:
                            return qsTr("No newer compatible release is available for this build.")
                        case UpdateModel.CheckFailed:
                            return qsTr("An error occurred while checking for updates...")
                        case UpdateModel.Checking:
                            return qsTr("Checking for updates...")
                        default:
                            return qsTr("N/A")
                    }
                }

                onTextChanged: {
                    if (visible) {
                        const window = Window.window
                        if (window)
                            window.alert(0)
                    }
                }

                elide: Text.ElideRight
                textFormat: Text.PlainText

                font.pixelSize: VLCStyle.fontSize_large

                color: UpdateModel.updateStatus === UpdateModel.NeedUpdate ? theme.fg.positive
                     : UpdateModel.updateStatus === UpdateModel.CheckFailed ? theme.fg.negative : theme.fg.primary
            }

            Widgets.ActionButtonOverlay {
                id: revealDetailsButton

                AccessibleCompat.id: "updateDetailsButton"

                iconTxt: VLCIcons.chevron_up
                iconRotation: descriptionFlickable.revealed ? 180 : 0

                Behavior on iconRotation {
                    // WARNING: Can't use `RotationAnimator` because it expects property to be "rotation".
                    RotationAnimation {
                        easing.type: Easing.InOutSine
                        duration: heightAnimation.duration
                    }
                }

                text: qsTr("Details")
                showText: !rowLayout.collapseText

                // Do not assume that description would be available depending on specific update status.
                // This button must be available at any time if the model provides description.
                visible: (descriptionLabel.text.length > 0)

                onClicked: {
                    descriptionFlickable.revealed = !descriptionFlickable.revealed
                }

                Navigation.parentItem: root
                Navigation.rightItem: recheckButton
            }

            component Separator : Rectangle {
                id: separator

                implicitWidth: 1
                implicitHeight: titleLabel.height

                color: theme.border
            }

            Separator {
                visible: revealDetailsButton.visible
            }

            Widgets.ActionButtonOverlay {
                id: recheckButton

                iconTxt: VLCIcons.ic_fluent_arrow_sync_24_regular
                text: qsTr("Re-check")
                showText: !rowLayout.collapseText

                visible: (UpdateModel.updateStatus !== UpdateModel.Checking)

                onClicked: {
                    descriptionFlickable.revealed = false
                    UpdateModel.checkUpdate(true)
                }

                Navigation.parentItem: root
                Navigation.leftItem: revealDetailsButton
                Navigation.rightItem: downloadButton
            }

            Widgets.ActionButtonPrimary {
                id: downloadButton

                visible: (UpdateModel.updateStatus === UpdateModel.NeedUpdate)

                iconTxt: VLCIcons.ic_fluent_arrow_download_24_regular
                text: qsTr("View release on GitHub")
                showText: !rowLayout.collapseText

                Binding on focus {
                    when: downloadButton.visible
                    delayed: true // to steal focus from dismiss button
                    value: true
                }

                onClicked: {
                    if (UpdateModel.openReleasePage())
                        root.dismissRequested()
                }

                Navigation.parentItem: root
                Navigation.leftItem: recheckButton
                Navigation.rightItem: dismissButton
            }

            Separator {
                visible: recheckButton.visible || downloadButton.visible
            }

            Widgets.ActionButtonOverlay {
                id: dismissButton

                iconTxt: VLCIcons.close
                text: qsTr("Dismiss")
                showText: false // Always collapsed

                focus: true

                Component.onCompleted: {
                    clicked.connect(root, dismissRequested)
                }

                Navigation.parentItem: root
                Navigation.leftItem: downloadButton
            }
        }

        Widgets.LabelExt {
            width: parent.width
            visible: UpdateModel.updateStatus === UpdateModel.NeedUpdate && text.length > 0
            text: UpdateModel.subtitle
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: theme.fg.primary
            font.pixelSize: VLCStyle.fontSize_normal
        }

        Widgets.ProgressBarExt {
            anchors.left: parent.left
            anchors.right: parent.right

            horizontalPadding: 0

            from: 0
            to: 1

            indeterminate: (UpdateModel.updateStatus === UpdateModel.Checking)

            value: {
                switch (UpdateModel.updateStatus) {
                case UpdateModel.Checking:
                case UpdateModel.Downloading:
                    return UpdateModel.progress
                case UpdateModel.UpToDate:
                    return 1.0
                case UpdateModel.NeedUpdate:
                default:
                    return 0.0
                }
            }

            visible: {
                switch (UpdateModel.updateStatus) {
                case UpdateModel.Checking:
                case UpdateModel.Downloading:
                    return true
                case UpdateModel.UpToDate:
                case UpdateModel.NeedUpdate:
                default:
                    return false
                }
            }

            background: null
        }

        Flickable {
            id: descriptionFlickable

            anchors.left: parent.left
            anchors.right: parent.right

            implicitWidth: contentWidth
            implicitHeight: contentHeight

            contentWidth: width
            contentHeight: descriptionLabel.height

            interactive: height < descriptionLabel.height

            clip: !fadingEdge.effectCompatible && (height < implicitHeight)
            visible: height > 0.1

            property bool revealed: false

            height: revealed ? Math.min(implicitHeight, VLCStyle.dp(256, VLCStyle.scale)) : 0.1 // We want 0.0, but Qt 6.2.4 is bugged

            boundsBehavior: Flickable.StopAtBounds

            Behavior on height {
                enabled: root.animations

                NumberAnimation {
                    id: heightAnimation

                    easing.type: Easing.InOutSine
                    duration: VLCStyle.duration_long
                }
            }

            T.ScrollBar.vertical: Widgets.ScrollBarExt {}

            Widgets.LabelExt {
                id: descriptionLabel

                width: parent.width

                font.pixelSize: VLCStyle.fontSize_normal

                color: theme.fg.primary

                text: UpdateModel.description
                textFormat: Text.PlainText

                wrapMode: Text.WordWrap

                padding: VLCStyle.margin_xxsmall

                style: Text.Raised
                styleColor: Qt.rgba(0.0, 0.0, 0.0, 0.2)
            }

            Widgets.FadingEdge {
                id: fadingEdge

                parent: descriptionFlickable

                anchors.fill: parent

                backgroundColor: (root.background?.visible && root.background.color.a >= 1.0) ? root.background.color
                                                                                              : Qt.alpha(root.background.color, 0.0)

                sourceItem: descriptionFlickable.contentItem

                sourceX: descriptionFlickable.contentX
                sourceY: descriptionFlickable.contentY

                orientation: Qt.Vertical

                Binding on enableBeginningFade {
                    when: descriptionFlickable.atYBeginning
                    value: false
                }

                Binding on enableEndFade {
                    when: descriptionFlickable.atYEnd
                    value: false
                }
            }
        }
    }
}

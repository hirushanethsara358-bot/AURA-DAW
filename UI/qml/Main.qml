/// AURA DAW — main window: menu, transport, timeline, mixer, browser.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    required property var auraMain

    visible: true
    width: 1600
    height: 900
    minimumWidth: 1280
    minimumHeight: 720
    title: "AURA DAW 1.0.0 — " + auraMain.projectName
    color: "#14161c"

    menuBar: MenuBar {
        Menu {
            title: "&File"
            Action { text: "&New Project"; shortcut: "Ctrl+N"; onTriggered: auraMain.newProject("Untitled") }
            Action { text: "&Open..."; shortcut: "Ctrl+O" }
            Action { text: "&Save"; shortcut: "Ctrl+S" }
            MenuSeparator {}
            Action { text: "E&xit"; shortcut: "Ctrl+Q"; onTriggered: Qt.quit() }
        }
        Menu {
            title: "&Edit"
            Action { text: "&Undo"; shortcut: "Ctrl+Z" }
            Action { text: "&Redo"; shortcut: "Ctrl+Y" }
        }
        Menu {
            title: "&View"
            Action { text: "&Mixer"; shortcut: "F3"; checkable: true; checked: true; onToggled: mixerPanel.visible = checked }
            Action { text: "&Browser"; shortcut: "F4"; checkable: true; checked: true; onToggled: browserPanel.visible = checked }
        }
        Menu {
            title: "&Help"
            Action { text: "&About AURA DAW"; onTriggered: aboutDialog.open() }
        }
    }

    header: TransportBar {
        auraMain: root.auraMain
    }

    RowLayout {
        anchors.fill: parent
        spacing: 1

        // Left: browser.
        Browser {
            id: browserPanel
            Layout.preferredWidth: 260
            Layout.fillHeight: true
        }

        // Center: arrangement timeline (placeholder grid + demo clips).
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1a1d25"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Time ruler.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "#20242e"
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: 16
                            delegate: Label {
                                width: 80
                                text: "  " + (index + 1)
                                color: "#8b93a7"
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                                height: 28
                            }
                        }
                    }
                }

                // Track lanes with demo clips.
                Repeater {
                    model: ["Drums", "Bass", "Keys", "Vox"]
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 64
                        color: index % 2 === 0 ? "#1a1d25" : "#1d2029"
                        border.color: "#262b37"
                        border.width: 1
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 90
                            spacing: 6
                            Rectangle {
                                width: 220; height: 44; anchors.verticalCenter: parent.verticalCenter
                                radius: 4; color: "#3b5bd6"
                                Label { anchors.centerIn: parent; text: modelData + " — Intro"; color: "white"; font.pixelSize: 11 }
                            }
                            Rectangle {
                                width: 160; height: 44; anchors.verticalCenter: parent.verticalCenter
                                radius: 4; color: "#7a3bd6"
                                Label { anchors.centerIn: parent; text: "Verse"; color: "white"; font.pixelSize: 11 }
                            }
                        }
                        Label {
                            x: 10; anchors.verticalCenter: parent.verticalCenter
                            text: modelData; color: "#c6ccdb"; font.pixelSize: 12; font.bold: true
                        }
                    }
                }
            }
        }

        // Right: inspector.
        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            color: "#181b22"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                Label { text: "Inspector"; color: "#e8ebf3"; font.pixelSize: 14; font.bold: true }
                Label { text: "Track: Keys"; color: "#8b93a7" }
                Label { text: "Tempo: " + auraMain.tempo.toFixed(1) + " BPM"; color: "#8b93a7" }
                Label { text: "CPU: " + (auraMain.cpuLoad * 100).toFixed(1) + " %"; color: "#8b93a7" }
                Item { Layout.fillHeight: true }
            }
        }
    }

    footer: MixerView {
        id: mixerPanel
        Layout.preferredHeight: 220
    }

    Dialog {
        id: aboutDialog
        title: "About AURA DAW"
        anchors.centerIn: parent
        Label { text: "AURA Digital Audio Workstation 1.0.0\nBuilt with C++20 · Qt 6 · JUCE" }
        standardButtons: Dialog.Ok
    }
}

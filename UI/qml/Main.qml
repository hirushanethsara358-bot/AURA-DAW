/// AURA DAW 0.1 — project/timeline shell, with no demo clips.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root
    required property var auraMain

    visible: true
    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 680
    title: "AURA DAW 0.1 — " + auraMain.projectName + (auraMain.dirty ? " *" : "")
    color: "#14161c"

    menuBar: MenuBar {
        Menu {
            title: "&File"
            Action {
                text: "&New Project"
                shortcut: "Ctrl+N"
                onTriggered: auraMain.dirty ? discardChangesDialog.open() : newProjectDialog.open()
            }
            Action {
                text: "&Open..."
                shortcut: "Ctrl+O"
                onTriggered: openDialog.open()
            }
            Action {
                text: "&Save"
                shortcut: "Ctrl+S"
                onTriggered: auraMain.projectPath.length > 0
                             ? auraMain.saveProject(auraMain.projectPath)
                             : saveDialog.open()
            }
            Action { text: "Save &As..."; onTriggered: saveDialog.open() }
            MenuSeparator {}
            Action { text: "Import &WAV..."; onTriggered: wavDialog.open() }
            MenuSeparator {}
            Action { text: "E&xit"; shortcut: "Ctrl+Q"; onTriggered: Qt.quit() }
        }
        Menu {
            title: "&Edit"
            Action { text: "&Undo"; shortcut: "Ctrl+Z"; enabled: false }
            Action { text: "&Redo"; shortcut: "Ctrl+Y"; enabled: false }
        }
        Menu {
            title: "&View"
            Action {
                text: "&Mixer"
                shortcut: "F3"
                checkable: true
                checked: true
                onToggled: mixerPanel.visible = checked
            }
            Action {
                text: "&Browser"
                shortcut: "F4"
                checkable: true
                checked: true
                onToggled: browserPanel.visible = checked
            }
        }
        Menu {
            title: "&Help"
            Action { text: "&About AURA DAW"; onTriggered: aboutDialog.open() }
        }
    }

    header: TransportBar { auraMain: root.auraMain }

    RowLayout {
        anchors.fill: parent
        spacing: 1

        Browser {
            id: browserPanel
            Layout.preferredWidth: 230
            Layout.fillHeight: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1a1d25"

            ScrollView {
                id: timelineScroll
                anchors.fill: parent
                clip: true

                Column {
                    id: timelineContent
                    width: Math.max(timelineScroll.availableWidth, 1500)
                    spacing: 0

                    Rectangle {
                        width: parent.width
                        height: 30
                        color: "#20242e"
                        Row {
                            anchors.fill: parent
                            Item { width: 112; height: parent.height }
                            Repeater {
                                model: 16
                                delegate: Label {
                                    required property int index
                                    width: 80
                                    height: 30
                                    text: "Bar " + (index + 1)
                                    color: "#8b93a7"
                                    font.pixelSize: 10
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }
                        }
                        Rectangle {
                            x: 112 + auraMain.playheadBeats * 20
                            width: 2
                            height: parent.height
                            color: "#ff667e"
                            visible: auraMain.playbackReady
                            z: 5
                        }
                    }

                    Repeater {
                        model: auraMain.tracks
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            property var track: modelData
                            width: timelineContent.width
                            height: 64
                            color: index % 2 === 0 ? "#1a1d25" : "#1d2029"
                            border.color: "#262b37"
                            border.width: 1

                            Label {
                                x: 10
                                width: 96
                                anchors.verticalCenter: parent.verticalCenter
                                text: track.name
                                color: "#c6ccdb"
                                font.pixelSize: 12
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Repeater {
                                model: track.clips
                                delegate: Rectangle {
                                    required property var modelData
                                    property var clipData: modelData
                                    x: 112 + clipData.startBar * 80
                                    width: Math.min(1200, Math.max(48, clipData.lengthBars * 80))
                                    height: 42
                                    anchors.verticalCenter: parent.verticalCenter
                                    radius: 4
                                    color: track.type === "audio" ? "#3b5bd6" : "#7a3bd6"
                                    border.color: "#7890ff"
                                    Label {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        text: clipData.name
                                        color: "white"
                                        font.pixelSize: 11
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            Rectangle {
                                x: 112 + auraMain.playheadBeats * 20
                                width: 2
                                height: parent.height
                                color: "#ff667e"
                                visible: auraMain.playbackReady
                                z: 5
                            }
                        }
                    }

                    Label {
                        visible: auraMain.tracks.length === 0
                        width: timelineContent.width
                        height: 120
                        text: "Create a project, then choose File → Import WAV to add the first arrangement clip."
                        color: "#8b93a7"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 245
            Layout.fillHeight: true
            color: "#181b22"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                Label { text: "Project"; color: "#e8ebf3"; font.pixelSize: 14; font.bold: true }
                Label { text: auraMain.projectName; color: "#c6ccdb"; wrapMode: Text.Wrap }
                Label { text: "Tracks: " + auraMain.tracks.length; color: "#8b93a7" }
                Label { text: "Tempo: " + auraMain.tempo.toFixed(1) + " BPM"; color: "#8b93a7" }
                Label { text: auraMain.dirty ? "Unsaved changes" : "Saved"; color: auraMain.dirty ? "#e6b450" : "#70b881" }
                Item { Layout.fillHeight: true }
                Label {
                    text: "0.1 playback supports one WAV clip at a time through the default shared-mode audio output."
                    color: "#697287"
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }

    footer: ColumnLayout {
        spacing: 0
        MixerView {
            id: mixerPanel
            auraMain: root.auraMain
            Layout.fillWidth: true
            Layout.preferredHeight: 220
        }
        ToolBar {
            Layout.fillWidth: true
            implicitHeight: 28
            background: Rectangle { color: "#20242e" }
            Label {
                anchors.fill: parent
                anchors.leftMargin: 12
                text: auraMain.statusMessage
                color: "#aeb6c8"
                font.pixelSize: 11
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    FileDialog {
        id: openDialog
        title: "Open AURA Project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["AURA project (*.aura)", "All files (*)"]
        onAccepted: auraMain.loadProject(selectedFile.toLocalFile())
    }

    FileDialog {
        id: saveDialog
        title: "Save AURA Project"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "aura"
        nameFilters: ["AURA project (*.aura)"]
        onAccepted: auraMain.saveProject(selectedFile.toLocalFile())
    }

    FileDialog {
        id: wavDialog
        title: "Import WAV to Arrangement"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Supported WAV audio (*.wav)", "All files (*)"]
        onAccepted: auraMain.importWav(selectedFile.toLocalFile())
    }

    Dialog {
        id: newProjectDialog
        title: "New Project"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            implicitWidth: 320
            Label { text: "Project name" }
            TextField { id: projectNameInput; text: "Untitled"; Layout.fillWidth: true }
        }
        onAccepted: auraMain.newProject(projectNameInput.text)
    }

    Dialog {
        id: discardChangesDialog
        implicitWidth: 360
        title: "Unsaved changes"
        modal: true
        contentItem: Label {
            text: "Discard the current project changes?"
            padding: 16
        }
        footer: RowLayout {
            Button { text: "Cancel"; onClicked: discardChangesDialog.close() }
            Button {
                text: "Discard"
                onClicked: {
                    discardChangesDialog.close()
                    newProjectDialog.open()
                }
            }
        }
    }

    Dialog {
        id: errorDialog
        implicitWidth: 480
        title: "AURA DAW"
        modal: true
        property string message: ""
        standardButtons: Dialog.Ok
        contentItem: Label {
            text: errorDialog.message
            wrapMode: Text.Wrap
            padding: 16
        }
    }

    Connections {
        target: auraMain
        function onOperationFailed(message) {
            errorDialog.message = message
            errorDialog.open()
        }
    }

    Dialog {
        id: aboutDialog
        implicitWidth: 320
        title: "About AURA DAW"
        standardButtons: Dialog.Ok
        contentItem: Label {
            text: "AURA DAW 0.1 MVP prototype\nC++20 · Qt 6"
            padding: 16
        }
    }
}

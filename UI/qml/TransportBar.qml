/// AURA DAW — transport controls, playhead and audio-device status.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    required property var auraMain
    background: Rectangle { color: "#20242e" }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        spacing: 7

        Label { text: "◉ AURA"; color: "#7c8aff"; font.pixelSize: 16; font.bold: true }

        ToolSeparator {}

        Button {
            contentItem: Label { text: "⏮"; font.pixelSize: 16 }
            ToolTip.visible: hovered
            ToolTip.text: "Return playhead to start"
            onClicked: auraMain.rewind()
        }
        Button {
            contentItem: Label { text: auraMain.playing ? "⏸" : "▶"; font.pixelSize: 16 }
            ToolTip.visible: hovered
            ToolTip.text: auraMain.playing ? "Pause" : "Play"
            onClicked: auraMain.playing ? auraMain.stop() : auraMain.play()
        }
        Button {
            enabled: false
            ToolTip.visible: hovered
            ToolTip.text: "Recording is not in the 0.1 MVP"
            contentItem: Label { text: "⏺"; font.pixelSize: 16; color: "#697287" }
        }
        Button {
            contentItem: Label { text: "⏹"; font.pixelSize: 16 }
            ToolTip.visible: hovered
            ToolTip.text: "Stop"
            onClicked: auraMain.stop()
        }

        ToolSeparator {}

        Label { text: "Tempo"; color: "#8b93a7"; font.pixelSize: 11 }
        SpinBox {
            from: 20; to: 999; value: Math.round(auraMain.tempo)
            editable: true; implicitWidth: 96
            onValueChanged: auraMain.setTempo(value)
        }

        Slider {
            id: playheadSlider
            Layout.preferredWidth: 150
            from: 0
            to: Math.max(4, auraMain.timelineLengthBeats)
            stepSize: 0
            ToolTip.visible: pressed
            ToolTip.text: "Seek through the arrangement"
            onMoved: auraMain.seekBeats(value)
        }
        Binding {
            target: playheadSlider
            property: "value"
            value: auraMain.playheadBeats
            when: !playheadSlider.pressed
        }

        Label {
            text: "Bar.Beat  " + Math.floor(auraMain.playheadBeats / 4 + 1) + "." +
                  (Math.floor(auraMain.playheadBeats % 4) + 1)
            color: "#c6ccdb"
            font.pixelSize: 12
        }

        Item { Layout.fillWidth: true }

        Button {
            visible: auraMain.playbackReady && !auraMain.audioOutputRunning
            text: "Retry audio"
            ToolTip.visible: hovered
            ToolTip.text: "Reopen the default WASAPI output"
            onClicked: auraMain.restartAudio()
        }
        Label {
            text: "Underruns " + auraMain.underrunCount
            color: auraMain.underrunCount > 0 ? "#e06c75" : "#778096"
            font.pixelSize: 10
            ToolTip.visible: hovered
            ToolTip.text: "Callback blocks exceeding the one-buffer processing budget"
        }
        Label {
            text: auraMain.audioOutputStatus
            color: auraMain.audioOutputRunning ? "#70b881" : "#778096"
            font.pixelSize: 10
            elide: Text.ElideMiddle
            Layout.maximumWidth: 360
        }
    }
}

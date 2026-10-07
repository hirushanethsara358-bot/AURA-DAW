/// AURA DAW — transport controls (play/stop/record/tempo/position).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    required property var auraMain
    background: Rectangle { color: "#20242e" }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        spacing: 8

        Label { text: "◉ AURA"; color: "#7c8aff"; font.pixelSize: 16; font.bold: true }

        ToolSeparator {}

        Button { contentItem: Label { text: "⏮"; font.pixelSize: 16 }; onClicked: auraMain.stop() }
        Button { contentItem: Label { text: auraMain.playing ? "⏸" : "▶"; font.pixelSize: 16 }; onClicked: auraMain.playing ? auraMain.stop() : auraMain.play() }
        Button { contentItem: Label { text: "⏺"; font.pixelSize: 16; color: "#e04848" }; onClicked: auraMain.record() }
        Button { contentItem: Label { text: "⏹"; font.pixelSize: 16 }; onClicked: auraMain.stop() }

        ToolSeparator {}

        Label { text: "Tempo"; color: "#8b93a7"; font.pixelSize: 11 }
        SpinBox {
            from: 20; to: 999; value: Math.round(auraMain.tempo)
            editable: true; implicitWidth: 110
            onValueChanged: auraMain.setTempo(value)
        }

        Label { text: "Bar.Beat  " + Math.floor(auraMain.playheadBeats / 4 + 1) + "." + (Math.floor(auraMain.playheadBeats % 4) + 1); color: "#c6ccdb"; font.pixelSize: 12 }

        Item { Layout.fillWidth: true }

        Label { text: "44.1–192 kHz · 64-bit float · ASIO/WASAPI"; color: "#5b6376"; font.pixelSize: 11 }
    }
}

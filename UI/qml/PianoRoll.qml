/// AURA DAW — piano roll MIDI editor.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    color: "#14161c"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Piano keys.
        Column {
            Layout.preferredWidth: 72
            Layout.fillHeight: true
            Repeater {
                model: 24
                delegate: Rectangle {
                    width: 72; height: 22
                    color: [1, 3, 6, 8, 10].includes((72 - index) % 12) ? "#0c0d11" : "#dfe3ee"
                    border.color: "#2e3444"
                    Label {
                        anchors.right: parent.right; anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: ["B", "A#", "A", "G#", "G", "F#", "F", "E", "D#", "D", "C#", "C"][(72 - index) % 12] + Math.floor((72 - index) / 12 - 1)
                        font.pixelSize: 9
                        color: parent.color === "#0c0d11" ? "#8b93a7" : "#333"
                    }
                }
            }
        }

        // Note grid with demo notes.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1a1d25"
            Grid {
                anchors.fill: parent
                columns: 16
                rows: 24
                Repeater {
                    model: 16 * 24
                    delegate: Rectangle {
                        width: parent.width / 16; height: parent.height / 24
                        color: "transparent"
                        border.color: "#22262f"
                        border.width: 0.5
                    }
                }
            }
            // Demo melody notes.
            Repeater {
                model: [
                    { "key": 4, "step": 0, "len": 2 }, { "key": 6, "step": 2, "len": 2 },
                    { "key": 7, "step": 4, "len": 4 }, { "key": 6, "step": 8, "len": 2 },
                    { "key": 4, "step": 10, "len": 2 }, { "key": 2, "step": 12, "len": 4 }
                ]
                delegate: Rectangle {
                    x: (modelData.step / 16) * parent.width + 1
                    y: (modelData.key / 24) * parent.height + 1
                    width: (modelData.len / 16) * parent.width - 2
                    height: parent.height / 24 - 2
                    radius: 2
                    color: "#3bd671"
                    border.color: "#1d7a3f"
                }
            }
        }
    }
}

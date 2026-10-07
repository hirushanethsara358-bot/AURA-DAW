/// AURA DAW — mixer panel with channel strips.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    color: "#181b22"
    implicitHeight: 220

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Repeater {
            model: ["Drums", "Bass", "Keys", "Vox", "Bus 1", "Master"]
            delegate: Rectangle {
                Layout.preferredWidth: 110
                Layout.fillHeight: true
                radius: 6
                color: modelData === "Master" ? "#262b3d" : "#20242e"
                border.color: "#2e3444"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    Label { text: modelData; color: "#e8ebf3"; font.bold: true; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
                    Slider {
                        Layout.alignment: Qt.AlignHCenter
                        orientation: Qt.Vertical
                        Layout.fillHeight: true
                        from: -60; to: 6; value: 0
                    }
                    Label { text: "0.0 dB"; color: "#8b93a7"; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        Button { text: "M"; checkable: true; implicitWidth: 32; implicitHeight: 24 }
                        Button { text: "S"; checkable: true; implicitWidth: 32; implicitHeight: 24 }
                    }
                }
            }
        }
        Item { Layout.fillWidth: true }
    }
}

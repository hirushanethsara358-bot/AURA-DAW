/// AURA DAW 0.1 — live track fader and pan controls for the playback path.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    required property var auraMain
    color: "#181b22"
    implicitHeight: 220

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Mixer — track fader and pan are live during playback"
                color: "#8b93a7"
                font.pixelSize: 10
            }
            Item { Layout.fillWidth: true }
            Label {
                text: "CPU " + Math.round(auraMain.cpuLoad * 100) + "%  ·  Peak " +
                      (auraMain.masterPeak > 0.000001
                       ? (20 * Math.log(auraMain.masterPeak) / Math.LN10).toFixed(1) + " dB"
                       : "−∞ dB")
                color: "#8b93a7"
                font.pixelSize: 10
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Row {
                id: stripsRow
                spacing: 6
                height: parent.height

                Repeater {
                    model: auraMain.tracks
                    delegate: Rectangle {
                        required property var modelData
                        property var track: modelData
                        width: 124
                        height: stripsRow.height
                        radius: 6
                        color: "#20242e"
                        border.color: "#2e3444"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 7
                            spacing: 4

                            Label {
                                text: track.name
                                color: "#e8ebf3"
                                font.bold: true
                                font.pixelSize: 11
                                Layout.alignment: Qt.AlignHCenter
                                elide: Text.ElideRight
                            }

                            Label {
                                text: gainSlider.value.toFixed(1) + " dB"
                                color: "#8b93a7"
                                font.pixelSize: 10
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Slider {
                                id: gainSlider
                                Layout.fillWidth: true
                                from: -96
                                to: 12
                                value: track.volumeDb
                                onMoved: auraMain.setTrackVolumeDb(track.id, value)
                            }

                            Label {
                                text: "Pan " + (panSlider.value < 0 ? "L " : panSlider.value > 0 ? "R " : "C ") + Math.round(Math.abs(panSlider.value) * 100) + "%"
                                color: "#8b93a7"
                                font.pixelSize: 10
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Slider {
                                id: panSlider
                                Layout.fillWidth: true
                                from: -1
                                to: 1
                                value: track.pan
                                onMoved: auraMain.setTrackPan(track.id, value)
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                Label {
                    visible: auraMain.tracks.length === 0
                    width: 260
                    height: stripsRow.height
                    text: "Import WAV to create a track."
                    color: "#697287"
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}

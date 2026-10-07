/// AURA DAW — left browser (samples, plugins, projects).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    color: "#181b22"

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        Label { text: "Browser"; color: "#e8ebf3"; font.pixelSize: 14; font.bold: true }

        TabBar {
            id: tabs
            width: parent.width - 16
            TabButton { text: "Samples" }
            TabButton { text: "Plugins" }
            TabButton { text: "Projects" }
        }

        StackLayout {
            width: parent.width - 16
            height: parent.height - 110
            currentIndex: tabs.currentIndex

            ListView {
                model: ["Kick 01.wav", "Snare 02.wav", "Hat Closed.wav", "Bass Loop 120.wav", "Pad Am.wav"]
                delegate: ItemDelegate { text: modelData; width: ListView.view.width }
            }
            ListView {
                model: ["AURA Synth", "AURA Sampler", "Para EQ", "Comp", "Reverb", "Delay"]
                delegate: ItemDelegate { text: modelData; width: ListView.view.width }
            }
            ListView {
                model: ["Demo Song.aura", "My Beat.aura"]
                delegate: ItemDelegate { text: modelData; width: ListView.view.width }
            }
        }
    }
}

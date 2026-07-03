import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OneRSS

Dialog {
    id: root
    anchors.centerIn: parent
    modal: true
    focus: true
    title: qsTr("Settings")
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: Math.min(parent ? parent.width - 80 : 760, 760)
    height: Math.min(parent ? parent.height - 80 : 720, 720)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        TabBar {
            id: tabBar
            Layout.fillWidth: true

            TabButton { text: qsTr("General") }
            TabButton { text: qsTr("Logging") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            GeneralSettings {
                id: generalSettings
            }

            LoggingSettings {
                id: loggingSettings
            }
        }
    }

    onOpened: {
        generalSettings.reload()
        loggingSettings.reload()
    }
    onAccepted: {
        generalSettings.commit()
        loggingSettings.commit()
        close()
    }
    onRejected: {
        generalSettings.revert()
        loggingSettings.revert()
        close()
    }
}

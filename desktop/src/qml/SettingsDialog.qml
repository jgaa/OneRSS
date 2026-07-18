import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OneRSS

Dialog {
    id: root
    property int initialAndroidFeedTreeScalePercent: 112
    signal androidFeedTreeScalePercentCommitted(int value)
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
            TabButton { text: qsTr("Archive") }
            TabButton { text: qsTr("Browsers") }
            TabButton { text: qsTr("Logging") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            GeneralSettings {
                id: generalSettings
                androidFeedTreeScalePercent: root.initialAndroidFeedTreeScalePercent
                onAndroidFeedTreeScalePercentCommitted: function(value) {
                    root.androidFeedTreeScalePercentCommitted(value)
                }
            }

            ArchiveSettings {
                id: archiveSettings
            }

            BrowserSettings {
                id: browserSettings
            }

            LoggingSettings {
                id: loggingSettings
            }
        }
    }

    onOpened: {
        generalSettings.reload()
        archiveSettings.reload(mainViewModel.defaultArchiveMode, mainViewModel.defaultArchiveLimit)
        browserSettings.reload()
        loggingSettings.reload()
    }
    onAccepted: {
        generalSettings.commit()
        mainViewModel.updateUserSettings(generalSettings.refreshIntervalHours,
                                         archiveSettings.selectedMode,
                                         archiveSettings.retentionLimit)
        browserSettings.commit()
        loggingSettings.commit()
        close()
    }
    onRejected: {
        generalSettings.revert()
        archiveSettings.reload(mainViewModel.defaultArchiveMode, mainViewModel.defaultArchiveLimit)
        browserSettings.revert()
        loggingSettings.revert()
        close()
    }
}

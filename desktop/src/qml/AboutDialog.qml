import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OneRSS

Dialog {
    id: root
    anchors.centerIn: parent
    modal: true
    focus: true
    title: qsTr("About OneRSS")
    standardButtons: Dialog.Ok
    width: Math.min(parent ? parent.width - 80 : 560, 560)
    height: Math.min(parent ? parent.height - 80 : implicitHeight, 560)
    padding: 16

    contentItem: ScrollView {
        id: scrollView
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        contentWidth: availableWidth

        Column {
            width: scrollView.availableWidth
            spacing: 16

            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                text: AppInfo.description
            }

            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: 16
                rowSpacing: 8

                Label { text: qsTr("App version") }
                Label { text: AppInfo.applicationVersion }

                Label { text: qsTr("Qt version") }
                Label { text: AppInfo.qtVersion }

                Label { text: qsTr("TLS library") }
                Label { text: AppInfo.tlsLibraryVersion }

                Label { text: qsTr("Protobuf version") }
                Label { text: AppInfo.protobufVersion }

                Label { text: qsTr("Compiler") }
                Label { text: AppInfo.compiler }

                Label { text: qsTr("Build date") }
                Label { text: AppInfo.buildDate }
            }

            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: 16
                rowSpacing: 8
                visible: mainViewModel.connectionState === 2 && mainViewModel.hasServerInfo

                Label { text: qsTr("Server version") }
                Label { text: mainViewModel.serverVersion }

                Label { text: qsTr("Database") }
                Label { text: mainViewModel.serverDatabaseName }

                Label { text: qsTr("Database version") }
                Label { text: mainViewModel.serverDatabaseVersion }
            }
        }
    }
}

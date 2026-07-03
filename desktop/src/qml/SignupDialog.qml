import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OneRSS

Dialog {
    id: root
    anchors.centerIn: parent
    modal: true
    focus: true
    title: qsTr("Sign Up / Pair Device")
    width: Math.min(parent ? parent.width - 80 : 620, 620)
    padding: 20

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 14
            rowSpacing: 10

            Label { text: qsTr("Server"); Layout.alignment: Qt.AlignRight | Qt.AlignVCenter }
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Server host")
                text: signupViewModel.serverHost
                onTextEdited: signupViewModel.serverHost = text
            }

            Label { text: qsTr("Port"); Layout.alignment: Qt.AlignRight | Qt.AlignVCenter }
            SpinBox {
                Layout.fillWidth: true
                from: 1
                to: 65535
                value: signupViewModel.serverPort
                onValueModified: signupViewModel.serverPort = value
            }

            Label { text: qsTr("Login"); Layout.alignment: Qt.AlignRight | Qt.AlignVCenter }
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Username or email")
                text: signupViewModel.login
                onTextEdited: signupViewModel.login = text
            }

            Label { text: qsTr("Password"); Layout.alignment: Qt.AlignRight | Qt.AlignVCenter }
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                text: signupViewModel.password
                onTextEdited: signupViewModel.password = text
            }

            Label { text: qsTr("Device"); Layout.alignment: Qt.AlignRight | Qt.AlignVCenter }
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Device name")
                text: signupViewModel.deviceName
                onTextEdited: signupViewModel.deviceName = text
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                text: qsTr("Create Account")
                enabled: !signupViewModel.busy
                Layout.fillWidth: true
                onClicked: signupViewModel.createAccount()
            }

            Button {
                text: qsTr("Pair Device")
                enabled: !signupViewModel.busy
                Layout.fillWidth: true
                onClicked: signupViewModel.pairDevice()
            }
        }

        Frame {
            Layout.fillWidth: true

            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                text: signupViewModel.status
            }
        }

        DialogButtonBox {
            Layout.fillWidth: true
            standardButtons: DialogButtonBox.Close
            onRejected: root.close()
        }
    }
}

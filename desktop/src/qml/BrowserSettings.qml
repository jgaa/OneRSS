import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    clip: true
    readonly property var manager: mainViewModel.browserProfileManager
    readonly property bool supported: manager && manager.customBrowserProfilesSupported

    function clearProfiles() {
        while (profileModel.count > 0) {
            profileModel.remove(profileModel.count - 1)
        }
    }

    function appendProfile(profile) {
        const id = profile ? profile["id"] : ""
        const displayName = profile ? profile["displayName"] : ""
        const command = profile ? profile["command"] : ""
        const argumentsValue = profile ? profile["arguments"] : ""
        const isDefault = profile ? profile["isDefault"] === true : false
        profileModel.append({
            profileId: id === undefined || id === null ? "" : String(id),
            displayName: displayName === undefined || displayName === null ? "" : String(displayName),
            command: command === undefined || command === null ? "" : String(command),
            cliArguments: argumentsValue === undefined || argumentsValue === null ? "" : String(argumentsValue),
            isDefault: isDefault
        })
    }

    function profileArray() {
        const profiles = []
        for (let i = 0; i < profileModel.count; ++i) {
            const profile = profileModel.get(i)
            profiles.push({
                id: profile.profileId,
                displayName: profile.displayName,
                command: profile.command,
                arguments: profile.cliArguments,
                isDefault: profile.isDefault
            })
        }
        return profiles
    }

    function setProfiles(profiles) {
        clearProfiles()
        for (const profile of profiles || []) {
            appendProfile(profile)
        }
    }

    function setDefaultProfile(index) {
        for (let i = 0; i < profileModel.count; ++i) {
            profileModel.setProperty(i, "isDefault", i === index)
        }
    }

    function addProfile() {
        appendProfile({})
    }

    function probeProfiles() {
        if (!supported) {
            return
        }
        setProfiles(manager.probeBrowserProfiles(profileArray()))
    }

    function commit() {
        if (!supported) {
            return
        }
        manager.browserProfiles = profileArray()
    }

    function reload() {
        if (!supported) {
            return
        }
        setProfiles(manager.browserProfiles || [])
    }

    function revert() {
        reload()
    }

    ListModel {
        id: profileModel
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 14

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.75
            text: supported
                  ? qsTr("Double-click uses the default profile. If no profile is marked as default, OneRSS uses the system default browser.")
                  : qsTr("Custom browser profiles are only available on Linux desktop builds.")
        }

        RowLayout {
            visible: supported
            spacing: 8

            Button {
                text: qsTr("Probe")
                onClicked: root.probeProfiles()
            }

            Button {
                text: qsTr("Add Profile")
                onClicked: root.addProfile()
            }
        }

        Label {
            visible: supported && profileModel.count === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.75
            text: qsTr("No browser profiles are configured yet.")
        }

        Repeater {
            model: profileModel

            Frame {
                required property int index
                required property string profileId
                required property string displayName
                required property string command
                required property string cliArguments
                required property bool isDefault
                Layout.fillWidth: true
                visible: supported

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        RadioButton {
                            checked: isDefault
                            text: qsTr("Default")
                            onClicked: root.setDefaultProfile(index)
                        }

                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Display name")
                            text: displayName
                            onTextEdited: profileModel.setProperty(index, "displayName", text)
                        }

                        Button {
                            text: qsTr("Remove")
                            onClicked: profileModel.remove(index)
                        }
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Command")
                        text: command
                        onTextEdited: profileModel.setProperty(index, "command", text)
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Optional arguments")
                        text: cliArguments
                        onTextEdited: profileModel.setProperty(index, "cliArguments", text)
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}

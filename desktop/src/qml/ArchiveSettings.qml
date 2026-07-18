import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    clip: true

    property bool feedMode: false
    property int selectedMode: 1
    property int retentionLimit: 1

    function reload(mode, limit) {
        selectedMode = feedMode ? mode : (mode === 0 ? 1 : mode)
        retentionLimit = Math.max(1, limit || 1)
    }

    function commit() {
        if (!feedMode) {
            mainViewModel.updateUserArchiveSettings(selectedMode, retentionLimit)
        }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: root.feedMode ? qsTr("Archive") : qsTr("Default archive policy")
            font.bold: true
        }

        ButtonGroup { id: archiveModes }

        RadioButton {
            visible: root.feedMode
            text: qsTr("Use default settings")
            ButtonGroup.group: archiveModes
            checked: root.selectedMode === 0
            onClicked: root.selectedMode = 0
        }

        RadioButton {
            text: qsTr("Keep all articles")
            ButtonGroup.group: archiveModes
            checked: root.selectedMode === 1
            onClicked: root.selectedMode = 1
        }

        RadioButton {
            text: qsTr("Limit archive to")
            ButtonGroup.group: archiveModes
            checked: root.selectedMode === 2
            onClicked: root.selectedMode = 2
        }

        SpinBox {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            from: 1
            to: 1000000
            editable: true
            enabled: root.selectedMode === 2
            value: root.retentionLimit
            onValueChanged: root.retentionLimit = value
            textFromValue: function(value) {
                return qsTr("%1 articles").arg(value)
            }
            valueFromText: function(text) {
                const match = text.match(/\d+/)
                return match ? Math.max(1, parseInt(match[0])) : root.retentionLimit
            }
        }

        RadioButton {
            text: qsTr("Delete articles older than")
            ButtonGroup.group: archiveModes
            checked: root.selectedMode === 3
            onClicked: root.selectedMode = 3
        }

        SpinBox {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            from: 1
            to: 36500
            editable: true
            enabled: root.selectedMode === 3
            value: root.retentionLimit
            onValueChanged: root.retentionLimit = value
            textFromValue: function(value) {
                return qsTr("%1 days").arg(value)
            }
            valueFromText: function(text) {
                const match = text.match(/\d+/)
                return match ? Math.max(1, parseInt(match[0])) : root.retentionLimit
            }
        }

        RadioButton {
            text: qsTr("Disable archiving")
            ButtonGroup.group: archiveModes
            checked: root.selectedMode === 4
            onClicked: root.selectedMode = 4
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("Disabled archiving keeps only articles currently received from the feed. Queued articles are retained.")
        }

        Item { Layout.fillHeight: true }
    }
}

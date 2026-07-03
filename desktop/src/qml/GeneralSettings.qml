import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    clip: true

    function commit() {
        mainViewModel.updateUserRefreshIntervalHours(refreshHours.value)
    }

    function reload() {
        refreshHours.value = Math.max(1, mainViewModel.defaultRefreshIntervalHours)
    }

    function revert() {
        reload()
    }

    GridLayout {
        id: formLayout
        width: root.availableWidth
        columns: width >= 420 ? 2 : 1
        rowSpacing: 8
        columnSpacing: 16

        Label { text: qsTr("Default Refresh\nInterval") }
        SpinBox {
            id: refreshHours
            Layout.fillWidth: true
            from: 1
            to: 24 * 365
            editable: true
            textFromValue: function(value) {
                return qsTr("%1 hours").arg(value)
            }
            valueFromText: function(text) {
                const match = text.match(/\d+/)
                return match ? Math.max(1, parseInt(match[0])) : mainViewModel.defaultRefreshIntervalHours
            }
        }

        Label {
            Layout.columnSpan: formLayout.columns
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("This server-stored setting is shared by all paired devices for the same account.")
        }

        Item { Layout.fillHeight: true }
    }
}

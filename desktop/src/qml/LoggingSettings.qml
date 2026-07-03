import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

ScrollView {
    id: root
    clip: true

    Settings {
        id: settings
    }

    function stringValue(key, fallback) {
        const value = settings.value(key, fallback)
        return value === undefined || value === null ? fallback : value.toString()
    }

    function intValue(key, fallback) {
        return parseInt(stringValue(key, fallback.toString()))
    }

    function commit() {
        settings.setValue("logging/path", logPath.text)
        settings.setValue("logging/filelevel", logLevelFile.currentIndex.toString())
        settings.setValue("logging/applevel", logLevelApp.currentIndex.toString())
        settings.setValue("logging/prune", prune.checked)
        settings.sync()
    }

    function reload() {
        logLevelApp.currentIndex = intValue("logging/applevel", 4)
        logLevelFile.currentIndex = intValue("logging/filelevel", 6)
        logPath.text = stringValue("logging/path", "/tmp/onerss-desktop.log")
        prune.checked = settings.value("logging/prune", true)
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

        Label { text: qsTr("Log Level\n(Application)") }
        ComboBox {
            id: logLevelApp
            Layout.fillWidth: true
            model: [
                qsTr("Disabled"),
                qsTr("Error"),
                qsTr("Warning"),
                qsTr("Notice"),
                qsTr("Info"),
                qsTr("Debug"),
                qsTr("Trace")
            ]
        }

        Label { text: qsTr("Log Level\n(File)") }
        ComboBox {
            id: logLevelFile
            Layout.fillWidth: true
            model: [
                qsTr("Disabled"),
                qsTr("Error"),
                qsTr("Warning"),
                qsTr("Notice"),
                qsTr("Info"),
                qsTr("Debug"),
                qsTr("Trace")
            ]
        }

        Label { text: qsTr("Log File") }
        TextField {
            id: logPath
            Layout.fillWidth: true
            placeholderText: qsTr("/tmp/onerss-desktop.log")
        }

        Item {}
        CheckBox {
            id: prune
            text: qsTr("Prune log file when starting")
        }

        Label {
            Layout.columnSpan: formLayout.columns
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("Logging changes apply on the next application start.")
        }

        Item { Layout.fillHeight: true }
    }
}

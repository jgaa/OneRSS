import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root

    required property var viewModel
    property bool narrowMode: false
    property string headerText: qsTr("Feeds")

    signal nodeActivated(string nodeId, int nodeType, bool synthetic, bool hasChildren)
    signal nodeExpansionRequested(string nodeId)
    signal nodeSelected(string nodeId)
    signal addFolderRequested(string nodeId)
    signal addFeedRequested(string nodeId)
    signal refreshRequested(string nodeId)
    signal markAllReadRequested(string nodeId)
    signal configureRequested(string nodeId)
    signal renameRequested(string nodeId)
    signal deleteRequested(string nodeId)

    padding: 0
    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            color: "#e8dfd2"
            implicitHeight: 52

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 16
                text: root.headerText
                font.pixelSize: 20
                font.bold: true
            }
        }

        ListView {
            id: feedList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.viewModel ? root.viewModel.feedTreeModel : null
            spacing: 1

            delegate: Rectangle {
                required property string nodeId
                required property string title
                required property int nodeType
                required property int depth
                required property bool expanded
                required property bool hasChildren
                required property bool synthetic

                width: ListView.view.width
                height: root.narrowMode ? 40 : 28
                color: root.viewModel && root.viewModel.selectedNodeId === nodeId
                       ? "#d8c3a5"
                       : ((treeMouse.containsMouse && !root.narrowMode) ? "#e9e2d7" : "transparent")

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10 + depth * 14
                    anchors.rightMargin: 8
                    spacing: root.narrowMode ? 8 : 6

                    Label {
                        visible: !root.narrowMode
                        width: 14
                        horizontalAlignment: Text.AlignHCenter
                        text: hasChildren ? (expanded ? "▾" : "▸") : ""
                    }

                    Label {
                        text: nodeType === 0 ? "📁" : "📰"
                        font.pixelSize: root.narrowMode ? 18 : 16
                    }

                    Label {
                        Layout.fillWidth: true
                        text: title
                        font.pixelSize: root.narrowMode ? 15 : 14
                        font.bold: synthetic
                        color: root.viewModel && root.viewModel.selectedNodeId === nodeId ? "#1f1a17" : "#2a2725"
                        elide: Text.ElideRight
                    }

                    ToolButton {
                        id: expandButton
                        visible: root.narrowMode && hasChildren
                        text: expanded ? "▾" : "▸"
                        onClicked: root.nodeExpansionRequested(nodeId)
                    }
                }

                HoverHandler {
                    id: treeMouse
                    enabled: !root.narrowMode
                }

                TapHandler {
                    enabled: !root.narrowMode
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onTapped: function(eventPoint, button) {
                        if (button === Qt.LeftButton) {
                            root.nodeActivated(nodeId, nodeType, synthetic, hasChildren)
                        } else if (button === Qt.RightButton) {
                            root.nodeSelected(nodeId)
                            treeMenu.popup()
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.rightMargin: expandButton.visible ? expandButton.width + 8 : 0
                    enabled: root.narrowMode
                    acceptedButtons: Qt.LeftButton
                    onClicked: root.nodeActivated(nodeId, nodeType, synthetic, hasChildren)
                    onPressAndHold: {
                        root.nodeSelected(nodeId)
                        treeMenu.popup()
                    }
                }

                Menu {
                    id: treeMenu

                    Action {
                        text: qsTr("Add Folder")
                        onTriggered: root.addFolderRequested(nodeId)
                    }

                    Action {
                        text: qsTr("Add Feed")
                        onTriggered: root.addFeedRequested(nodeId)
                    }

                    Action {
                        text: qsTr("Refresh")
                        enabled: nodeType === 1 && !synthetic
                        onTriggered: root.refreshRequested(nodeId)
                    }

                    Action {
                        text: qsTr("Mark All Read")
                        enabled: nodeType === 1 && !synthetic
                        onTriggered: root.markAllReadRequested(nodeId)
                    }

                    MenuSeparator {}

                    Action {
                        text: qsTr("Configure")
                        enabled: nodeType === 1 && !synthetic
                        onTriggered: root.configureRequested(nodeId)
                    }

                    Action {
                        text: qsTr("Rename")
                        enabled: !synthetic
                        onTriggered: root.renameRequested(nodeId)
                    }

                    Action {
                        text: qsTr("Delete")
                        enabled: !synthetic
                        onTriggered: root.deleteRequested(nodeId)
                    }
                }
            }
        }
    }
}

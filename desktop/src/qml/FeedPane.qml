import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root

    required property var viewModel
    property bool narrowMode: false
    property string headerText: qsTr("Feeds")
    property string draggedNodeId: ""
    property string dropTargetNodeId: ""
    property bool hasDropTarget: false
    property string draggedNodeTitle: ""
    property int draggedNodeType: 0
    property real dragPreviewX: 0
    property real dragPreviewY: 0

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
    signal moveNodeRequested(string nodeId, string newParentId)

    padding: 0
    Layout.fillWidth: true
    Layout.fillHeight: true

    function beginNodeDrag(nodeId, title, nodeType, x, y) {
        draggedNodeId = nodeId
        dropTargetNodeId = ""
        hasDropTarget = false
        draggedNodeTitle = title
        draggedNodeType = nodeType
        dragPreviewX = x
        dragPreviewY = y
    }

    function updateDropTarget(targetNodeId, syntheticTarget) {
        if (!viewModel || draggedNodeId.length === 0) {
            dropTargetNodeId = ""
            hasDropTarget = false
            return
        }

        const newParentId = syntheticTarget ? "" : targetNodeId
        hasDropTarget = viewModel.feedTreeModel.canReparent(draggedNodeId, newParentId)
        dropTargetNodeId = hasDropTarget ? newParentId : ""
    }

    function updateDragPreviewPosition(x, y) {
        dragPreviewX = x
        dragPreviewY = y
    }

    function finishNodeDrag() {
        if (draggedNodeId.length > 0
                && hasDropTarget
                && viewModel
                && viewModel.feedTreeModel.canReparent(draggedNodeId, dropTargetNodeId)) {
            moveNodeRequested(draggedNodeId, dropTargetNodeId)
        }
        draggedNodeId = ""
        dropTargetNodeId = ""
        hasDropTarget = false
        draggedNodeTitle = ""
        draggedNodeType = 0
    }

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
            interactive: root.draggedNodeId.length === 0
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

                readonly property bool validDropTarget: root.hasDropTarget
                                                        && root.draggedNodeId.length > 0
                                                        && root.dropTargetNodeId === (synthetic ? "" : nodeId)

                width: ListView.view.width
                height: root.narrowMode ? 40 : 28
                color: validDropTarget
                       ? "#b8d7a8"
                       : (root.viewModel && root.viewModel.selectedNodeId === nodeId
                          ? "#d8c3a5"
                          : ((treeMouse.containsMouse && !root.narrowMode) ? "#e9e2d7" : "transparent"))

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

                MouseArea {
                    anchors.fill: parent
                    enabled: !root.narrowMode
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    preventStealing: true

                    onPressed: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.nodeSelected(nodeId)
                        }
                    }

                    onPressAndHold: function(mouse) {
                        if (mouse.button !== Qt.LeftButton || synthetic) {
                            return
                        }
                        root.nodeSelected(nodeId)
                        const point = parent.mapToItem(root, mouse.x, mouse.y)
                        root.beginNodeDrag(nodeId, title, nodeType, point.x, point.y)
                    }

                    onPositionChanged: function(mouse) {
                        if (root.draggedNodeId.length === 0) {
                            return
                        }
                        const previewPoint = parent.mapToItem(root, mouse.x, mouse.y)
                        root.updateDragPreviewPosition(previewPoint.x, previewPoint.y)
                        const point = parent.mapToItem(feedList.contentItem, mouse.x, mouse.y)
                        const targetIndex = feedList.indexAt(point.x, point.y)
                        const targetItem = targetIndex >= 0 ? feedList.itemAtIndex(targetIndex) : null
                        root.updateDropTarget(targetItem ? targetItem.nodeId : "", targetItem ? targetItem.synthetic : false)
                    }

                    onReleased: function(mouse) {
                        if (root.draggedNodeId.length > 0) {
                            if (root.hasDropTarget && root.dropTargetNodeId.length > 0) {
                                root.viewModel.feedTreeModel.expandNode(root.dropTargetNodeId)
                            }
                            root.finishNodeDrag()
                            return
                        }
                        if (mouse.button === Qt.LeftButton) {
                            root.nodeActivated(nodeId, nodeType, synthetic, hasChildren)
                        } else if (mouse.button === Qt.RightButton) {
                            treeMenu.popup()
                        }
                    }

                    onCanceled: {
                        root.draggedNodeId = ""
                        root.dropTargetNodeId = ""
                        root.hasDropTarget = false
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

    Rectangle {
        visible: root.draggedNodeId.length > 0
        x: Math.max(8, Math.min(root.width - width - 8, root.dragPreviewX + 14))
        y: Math.max(8, Math.min(root.height - height - 8, root.dragPreviewY + 14))
        z: 1000
        radius: 8
        color: root.hasDropTarget ? "#dceccb" : "#f1e5d4"
        border.color: root.hasDropTarget ? "#7ca05e" : "#b59c7f"
        border.width: 1
        opacity: 0.95
        implicitWidth: previewRow.implicitWidth + 20
        implicitHeight: previewRow.implicitHeight + 14

        RowLayout {
            id: previewRow
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Label {
                text: root.draggedNodeType === 0 ? "📁" : "📰"
                font.pixelSize: 16
            }

            Label {
                text: root.draggedNodeTitle
                color: "#1f1a17"
                font.pixelSize: 14
                elide: Text.ElideRight
                Layout.preferredWidth: Math.min(260, implicitWidth)
            }
        }
    }
}

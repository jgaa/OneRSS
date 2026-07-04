import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root

    required property var viewModel
    property bool narrowMode: false
    property bool showBackButton: false
    property bool showPreviewButton: false
    property bool showOpenButton: false
    property string titleText: qsTr("Article List")
    property string emptySubtitle: qsTr("Your article list will appear here after feeds are added and synchronized.")
    property var formatTimestamp: function(value) { return value }

    signal backRequested()
    signal previewRequested()
    signal openInBrowserRequested()
    signal articlePreviewRequested(int row)

    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        anchors.fill: parent
        spacing: narrowMode ? 10 : 14

        RowLayout {
            Layout.fillWidth: true

            ToolButton {
                visible: root.showBackButton
                text: "\u2190"
                font.pixelSize: 22
                font.bold: true
                Accessible.name: qsTr("Back")
                onClicked: root.backRequested()
            }

            Label {
                Layout.fillWidth: true
                text: root.titleText
                font.pixelSize: 20
                font.bold: true
                elide: Text.ElideRight
            }
        }

        RowLayout {
            visible: root.showPreviewButton || root.showOpenButton
            Layout.fillWidth: true

            Button {
                visible: root.showPreviewButton
                text: qsTr("Preview")
                enabled: !!root.viewModel
                         && root.viewModel.articleListModel.selectedRow >= 0
                         && root.viewModel.hasPreview
                onClicked: root.previewRequested()
            }

            Button {
                visible: root.showOpenButton
                text: qsTr("Open in Browser")
                enabled: !!root.viewModel
                         && root.viewModel.articleListModel.selectedRow >= 0
                         && root.viewModel.hasPreview
                onClicked: root.openInBrowserRequested()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 420)
                spacing: 10
                visible: articleList.count === 0

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("No articles to show yet.")
                    font.pixelSize: 22
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: root.emptySubtitle
                }
            }

            ListView {
                id: articleList
                anchors.fill: parent
                clip: true
                model: root.viewModel ? root.viewModel.articleListModel : null
                visible: count > 0
                spacing: 0
                onContentYChanged: {
                    const remaining = contentHeight - (contentY + height)
                    if (root.viewModel && remaining < 320) {
                        root.viewModel.loadMoreArticles()
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AlwaysOn
                }

                delegate: Rectangle {
                    required property int index
                    required property bool isRead
                    required property bool selected
                    required property string title
                    required property string publishedAt
                    required property string author

                    function formattedMeta() {
                        const formattedTime = root.formatTimestamp ? root.formatTimestamp(publishedAt) : publishedAt
                        if (formattedTime.length > 0 && author.length > 0) {
                            return qsTr("%1 | %2").arg(formattedTime).arg(author)
                        }
                        if (formattedTime.length > 0) {
                            return formattedTime
                        }
                        return author
                    }

                    width: ListView.view.width
                    height: articleColumn.implicitHeight + (root.narrowMode ? 16 : 14)
                    color: selected ? "#cfe0c9" : (index % 2 === 0 ? "#ffffff" : "#f3f3f3")

                    Column {
                        id: articleColumn
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        anchors.topMargin: root.narrowMode ? 8 : 7
                        anchors.bottomMargin: root.narrowMode ? 8 : 7
                        spacing: 3

                        Label {
                            width: parent.width
                            text: title.length > 0 ? title : qsTr("(Untitled article)")
                            font.pixelSize: 15
                            font.bold: !isRead
                            color: isRead ? "#000000" : "#1d6f2a"
                            elide: Text.ElideRight
                        }

                        Label {
                            width: parent.width
                            font.pixelSize: 12
                            color: "#000000"
                            text: formattedMeta()
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: "#d7d7d7"
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: !root.narrowMode
                        onClicked: if (root.viewModel) root.viewModel.selectArticleRow(parent.index)
                        onDoubleClicked: {
                            if (!root.narrowMode && root.viewModel) {
                                root.viewModel.selectArticleRow(parent.index)
                                root.openInBrowserRequested()
                            }
                        }
                        onPressAndHold: {
                            if (root.narrowMode) {
                                root.articlePreviewRequested(parent.index)
                            }
                        }
                    }
                }
            }
        }
    }
}

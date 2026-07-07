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
    property bool showUnreadButton: false
    property string titleText: qsTr("Article List")
    property string emptySubtitle: qsTr("Your article list will appear here after feeds are added and synchronized.")
    property var formatTimestamp: function(value) { return value }

    signal backRequested()
    signal previewRequested()
    signal openInBrowserRequested()
    signal markUnreadRequested()
    signal articlePreviewRequested(int row)
    signal searchRequested(string query)

    function submitSearch() {
        if (root.viewModel) {
            root.viewModel.searchArticles(articleSearchField.text)
        }
        root.searchRequested(articleSearchField.text)
    }

    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        anchors.fill: parent
        spacing: narrowMode ? 10 : 14

        RowLayout {
            Layout.fillWidth: true

            ToolButton {
                visible: root.showBackButton
                implicitWidth: 40
                implicitHeight: 40
                contentItem: MaterialIcon {
                    text: "arrow_back"
                    iconSize: 24
                    color: "#263238"
                }
                Accessible.name: qsTr("Back")
                onClicked: root.backRequested()
            }

            Label {
                Layout.preferredWidth: implicitWidth
                text: root.titleText
                font.pixelSize: 20
                font.bold: true
                elide: Text.ElideRight
            }

            TextField {
                id: articleSearchField
                Layout.fillWidth: true
                placeholderText: qsTr("Search headlines")
                selectByMouse: true
                onAccepted: root.submitSearch()
            }

            ToolButton {
                implicitWidth: 38
                implicitHeight: 38
                contentItem: MaterialIcon {
                    text: "search"
                    iconSize: 22
                    color: "#263238"
                }
                Accessible.name: qsTr("Search")
                onClicked: root.submitSearch()
            }
        }

        RowLayout {
            visible: root.showPreviewButton || root.showOpenButton || root.showUnreadButton
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

            Button {
                visible: root.showUnreadButton
                text: qsTr("Unread")
                enabled: !!root.viewModel
                         && root.viewModel.articleListModel.selectedRow >= 0
                         && root.viewModel.selectedArticleIsRead
                onClicked: root.markUnreadRequested()
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

                delegate: Item {
                    required property int index
                    required property bool isRead
                    required property bool selected
                    required property string title
                    required property string feedTitle
                    required property string publishedAt
                    required property string author
                    required property string linkUrl

                    property real pressX: 0
                    property real pressY: 0
                    property bool swipeTriggered: false

                    function formattedMeta() {
                        const parts = []
                        if (feedTitle.length > 0) {
                            parts.push(feedTitle)
                        }
                        const formattedTime = root.formatTimestamp ? root.formatTimestamp(publishedAt) : publishedAt
                        if (formattedTime.length > 0) {
                            parts.push(formattedTime)
                        }
                        if (author.length > 0) {
                            parts.push(author)
                        }
                        return parts.join(" | ")
                    }

                    width: ListView.view.width
                    height: articleColumn.implicitHeight + (root.narrowMode ? 16 : 14)

                    function previewArticle() {
                        if (!root.viewModel) {
                            return
                        }
                        root.viewModel.selectArticleRow(index)
                        root.articlePreviewRequested(index)
                    }

                    function openInBrowser() {
                        if (!root.viewModel) {
                            return
                        }
                        root.viewModel.selectArticleRow(index)
                        root.openInBrowserRequested()
                    }

                    function showContextMenu() {
                        if (!root.viewModel) {
                            return
                        }
                        root.viewModel.selectArticleRow(index)
                        articleMenu.popup()
                    }

                    Rectangle {
                        anchors.fill: parent
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
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        hoverEnabled: !root.narrowMode
                        onPressed: function(mouse) {
                            pressX = mouse.x
                            pressY = mouse.y
                            swipeTriggered = false
                        }
                        onPositionChanged: function(mouse) {
                            if (!root.narrowMode || swipeTriggered || !(mouse.buttons & Qt.LeftButton)) {
                                return
                            }
                            const dx = mouse.x - pressX
                            const dy = mouse.y - pressY
                            if (dx < -56 && Math.abs(dx) > Math.abs(dy) + 12) {
                                swipeTriggered = true
                                previewArticle()
                            }
                        }
                        onClicked: function(mouse) {
                            if (swipeTriggered) {
                                return
                            }
                            if (mouse.button === Qt.RightButton) {
                                showContextMenu()
                                return
                            }
                            if (root.viewModel) {
                                root.viewModel.selectArticleRow(index)
                            }
                        }
                        onDoubleClicked: {
                            if (!root.narrowMode && root.viewModel) {
                                openInBrowser()
                            }
                        }
                        onPressAndHold: {
                            showContextMenu()
                        }
                    }

                    Menu {
                        id: articleMenu

                        MenuItem {
                            text: qsTr("Open in Browser")
                            enabled: linkUrl.length > 0
                            onTriggered: openInBrowser()
                        }

                        MenuItem {
                            visible: root.narrowMode
                            text: qsTr("Preview")
                            onTriggered: previewArticle()
                        }

                        MenuItem {
                            text: qsTr("Copy url")
                            enabled: linkUrl.length > 0
                            onTriggered: if (root.viewModel) root.viewModel.copyArticleUrl(index)
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: root.viewModel

        function onArticleSearchChanged() {
            articleSearchField.text = root.viewModel ? root.viewModel.articleSearchQuery : ""
        }
    }

    Component.onCompleted: {
        articleSearchField.text = root.viewModel ? root.viewModel.articleSearchQuery : ""
    }
}

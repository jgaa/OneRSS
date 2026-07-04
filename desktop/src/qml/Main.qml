import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OneRSS

ApplicationWindow {
    id: window
    width: 1320
    height: 820
    visible: true
    title: Qt.application.name
    color: "#f3efe7"
    onClosing: function(close) {
        if (typeof trayController !== "undefined" && trayController && trayController.trayAvailable) {
            close.accepted = false
            hide()
        }
    }

    property string contextNodeId: "__root__"
    readonly property bool narrowLayout: width < 860
    property int narrowPageIndex: 0
    property color statusColor: {
        switch (mainViewModel.statusBarSeverity) {
        case 1:
            return "#2e7d32"
        case 2:
            return "#a85f00"
        case 3:
            return "#b3261e"
        default:
            return "#546e7a"
        }
    }

    function openAddFolderDialog(nodeId) {
        contextNodeId = nodeId
        addFolderTitle.text = ""
        addFolderDialog.open()
    }

    function openAddFeedDialog(nodeId) {
        contextNodeId = nodeId
        addFeedTitle.text = ""
        addFeedUrl.text = ""
        addFeedComment.text = ""
        addFeedDialog.open()
    }

    function openRenameDialog(nodeId) {
        const node = mainViewModel.nodeData(nodeId)
        contextNodeId = nodeId
        renameTitle.text = node.title || ""
        renameDialog.open()
    }

    function openConfigureDialog(nodeId) {
        const node = mainViewModel.nodeData(nodeId)
        contextNodeId = nodeId
        configureTitle.text = node.title || ""
        configureUrl.text = node.feedUrl || ""
        configureComment.text = node.comment || ""
        configureUseDefault.checked = node.useDefaultRefreshInterval !== false
        configureRefreshHours.value = Math.max(1, node.refreshIntervalHours || mainViewModel.defaultRefreshIntervalHours)
        configureTabs.currentIndex = 0
        configureDialog.open()
    }

    function openDeleteDialog(nodeId) {
        contextNodeId = nodeId
        deleteDialog.open()
    }

    function showStatusDetails() {
        if (!mainViewModel.hasStatusBarDetail) {
            return
        }
        statusDetailsDialog.open()
    }

    function formatArticleTimestamp(value) {
        if (!value || value.length === 0) {
            return ""
        }

        const parsed = new Date(value)
        if (isNaN(parsed.getTime())) {
            return value
        }

        return Qt.locale().toString(parsed, Locale.ShortFormat)
    }

    function activateNode(nodeId, hasChildren) {
        mainViewModel.selectNode(nodeId)
        if (hasChildren) {
            mainViewModel.toggleExpanded(nodeId)
        }
    }

    function selectedNodeTitle() {
        const node = mainViewModel.nodeData(mainViewModel.selectedNodeId)
        if (node.title && node.title.length > 0) {
            return node.title
        }
        return qsTr("All Feeds")
    }

    function openNarrowArticles(nodeId) {
        mainViewModel.selectNode(nodeId)
        narrowPageIndex = 1
    }

    function openNarrowPreview(row) {
        mainViewModel.selectArticleRow(row)
        narrowPageIndex = 2
    }

    onNarrowLayoutChanged: {
        if (!narrowLayout) {
            narrowPageIndex = 0
        }
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("File")

            Action {
                text: qsTr("Sign Up / Pair Device...")
                onTriggered: signupDialog.open()
            }

            MenuSeparator {}

            Action {
                text: qsTr("Settings")
                onTriggered: settingsDialog.open()
            }

            MenuSeparator {}

            Action {
                text: qsTr("Quit")
                onTriggered: Qt.quit()
            }
        }

        Menu {
            title: qsTr("Help")

            Action {
                text: qsTr("About")
                onTriggered: aboutDialog.open()
            }
        }
    }

    SettingsDialog { id: settingsDialog; parent: Overlay.overlay }
    AboutDialog { id: aboutDialog; parent: Overlay.overlay }
    SignupDialog { id: signupDialog; parent: Overlay.overlay }

    Connections {
        target: mainViewModel

        function onFeedTitleLookupFinished(feedUrl, title) {
            const normalizedUrl = feedUrl.trim()

            if (addFeedDialog.visible
                    && addFeedTitle.text.trim().length === 0
                    && addFeedUrl.text.trim() === normalizedUrl
                    && title.trim().length > 0) {
                addFeedTitle.text = title
            }

            if (configureDialog.visible
                    && configureTitle.text.trim().length === 0
                    && configureUrl.text.trim() === normalizedUrl
                    && title.trim().length > 0) {
                configureTitle.text = title
            }
        }
    }

    Dialog {
        id: addFolderDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Add Folder")
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 400

        contentItem: ColumnLayout {
            Label { text: qsTr("Folder Name") }
            TextField { id: addFolderTitle; Layout.fillWidth: true }
        }

        onAccepted: mainViewModel.addFolder(contextNodeId, addFolderTitle.text)
    }

    Dialog {
        id: addFeedDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Add Feed")
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 460

        function updateOkEnabled() {
            addFeedDialog.standardButton(Dialog.Ok).enabled
                    = addFeedTitle.text.trim().length > 0
                    && addFeedUrl.text.trim().length > 0
        }

        contentItem: ColumnLayout {
            spacing: 10
            Label { text: qsTr("Feed Name") }
            TextField {
                id: addFeedTitle
                Layout.fillWidth: true
                placeholderText: qsTr("Leave empty to try the feed title")
                onTextChanged: {
                    addFeedDialog.updateOkEnabled()
                    if (text.trim().length === 0 && addFeedUrl.text.trim().length > 0) {
                        addFeedTitleLookupTimer.restart()
                    }
                }
            }
            Label { text: qsTr("Feed URL") }
            TextField {
                id: addFeedUrl
                Layout.fillWidth: true
                onTextChanged: {
                    addFeedDialog.updateOkEnabled()
                    if (text.trim().length > 0 && addFeedTitle.text.trim().length === 0) {
                        addFeedTitleLookupTimer.restart()
                    } else {
                        addFeedTitleLookupTimer.stop()
                    }
                }
            }
            Label { text: qsTr("Comment") }
            TextArea { id: addFeedComment; Layout.fillWidth: true; Layout.preferredHeight: 100 }
        }

        onOpened: updateOkEnabled()
        onAccepted: mainViewModel.addFeed(contextNodeId, addFeedTitle.text, addFeedUrl.text, addFeedComment.text)
    }

    Timer {
        id: addFeedTitleLookupTimer
        interval: 700
        repeat: false
        onTriggered: {
            if (addFeedDialog.visible
                    && addFeedTitle.text.trim().length === 0
                    && addFeedUrl.text.trim().length > 0) {
                mainViewModel.requestFeedTitleLookup(addFeedUrl.text)
            }
        }
    }

    Dialog {
        id: renameDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Rename")
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 400

        contentItem: ColumnLayout {
            Label { text: qsTr("Name") }
            TextField { id: renameTitle; Layout.fillWidth: true }
        }

        onAccepted: mainViewModel.renameNode(contextNodeId, renameTitle.text)
    }

    Dialog {
        id: configureDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Configure Feed")
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 460

        function updateOkEnabled() {
            configureDialog.standardButton(Dialog.Ok).enabled
                    = configureTitle.text.trim().length > 0
                    && configureUrl.text.trim().length > 0
        }

        contentItem: ColumnLayout {
            spacing: 12

            TabBar {
                id: configureTabs
                Layout.fillWidth: true

                TabButton { text: qsTr("General") }
                TabButton { text: qsTr("Refresh") }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: configureTabs.currentIndex

                ColumnLayout {
                    spacing: 10

                    Label { text: qsTr("Feed Name") }
                    TextField {
                        id: configureTitle
                        Layout.fillWidth: true
                        placeholderText: qsTr("Leave empty to try the feed title")
                        onTextChanged: {
                            configureDialog.updateOkEnabled()
                            if (text.trim().length === 0 && configureUrl.text.trim().length > 0) {
                                configureFeedTitleLookupTimer.restart()
                            }
                        }
                    }
                    Label { text: qsTr("Feed URL") }
                    TextField {
                        id: configureUrl
                        Layout.fillWidth: true
                        onTextChanged: {
                            configureDialog.updateOkEnabled()
                            if (text.trim().length > 0 && configureTitle.text.trim().length === 0) {
                                configureFeedTitleLookupTimer.restart()
                            } else {
                                configureFeedTitleLookupTimer.stop()
                            }
                        }
                    }
                    Label { text: qsTr("Comment") }
                    TextArea { id: configureComment; Layout.fillWidth: true; Layout.preferredHeight: 100 }
                }

                ColumnLayout {
                    spacing: 10

                    CheckBox {
                        id: configureUseDefault
                        text: qsTr("Use account default refresh interval")
                    }

                    Label {
                        text: qsTr("Account default: %1 hours").arg(mainViewModel.defaultRefreshIntervalHours)
                        opacity: 0.7
                    }

                    Label { text: qsTr("Custom Refresh Interval") }
                    SpinBox {
                        id: configureRefreshHours
                        Layout.fillWidth: true
                        from: 1
                        to: 24 * 365
                        editable: true
                        enabled: !configureUseDefault.checked
                        textFromValue: function(value) {
                            return qsTr("%1 hours").arg(value)
                        }
                        valueFromText: function(text) {
                            const match = text.match(/\d+/)
                            return match ? Math.max(1, parseInt(match[0])) : mainViewModel.defaultRefreshIntervalHours
                        }
                    }
                }
            }
        }

        onOpened: updateOkEnabled()
        onAccepted: mainViewModel.configureFeed(contextNodeId,
                                                configureTitle.text,
                                                configureUrl.text,
                                                configureComment.text,
                                                configureUseDefault.checked,
                                                configureRefreshHours.value)
    }

    Timer {
        id: configureFeedTitleLookupTimer
        interval: 700
        repeat: false
        onTriggered: {
            if (configureDialog.visible
                    && configureTitle.text.trim().length === 0
                    && configureUrl.text.trim().length > 0) {
                mainViewModel.requestFeedTitleLookup(configureUrl.text)
            }
        }
    }

    Dialog {
        id: deleteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Delete")
        standardButtons: Dialog.Yes | Dialog.No
        width: 380

        contentItem: ColumnLayout {
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Delete the selected item?")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("This also removes any child folders and feeds.")
            }
        }

        onAccepted: mainViewModel.deleteNode(contextNodeId)
    }

    Dialog {
        id: statusDetailsDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Message Details")
        standardButtons: Dialog.Ok
        width: 520

        contentItem: ScrollView {
            clip: true

            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                text: mainViewModel.statusBarDetail
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#f6f1e7" }
            GradientStop { position: 1.0; color: "#dde6e7" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 30
            color: "#e9e0d3"

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 12
                text: mainViewModel.connectionStatus
                font.pixelSize: 13
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            SplitView {
                anchors.fill: parent
                visible: !narrowLayout

                FeedPane {
                    SplitView.minimumWidth: 240
                    SplitView.preferredWidth: 320
                    viewModel: mainViewModel
                    onNodeActivated: function(nodeId, nodeType, synthetic, hasChildren) {
                        activateNode(nodeId, hasChildren)
                    }
                    onNodeSelected: function(nodeId) {
                        mainViewModel.selectNode(nodeId)
                    }
                    onNodeExpansionRequested: function(nodeId) {
                        mainViewModel.toggleExpanded(nodeId)
                    }
                    onAddFolderRequested: function(nodeId) {
                        openAddFolderDialog(nodeId)
                    }
                    onAddFeedRequested: function(nodeId) {
                        openAddFeedDialog(nodeId)
                    }
                    onRefreshRequested: function(nodeId) {
                        mainViewModel.refreshFeed(nodeId)
                    }
                    onMarkAllReadRequested: function(nodeId) {
                        mainViewModel.markAllArticlesRead(nodeId)
                    }
                    onConfigureRequested: function(nodeId) {
                        openConfigureDialog(nodeId)
                    }
                    onRenameRequested: function(nodeId) {
                        openRenameDialog(nodeId)
                    }
                    onDeleteRequested: function(nodeId) {
                        openDeleteDialog(nodeId)
                    }
                }

                SplitView {
                    orientation: Qt.Vertical

                    ArticleListPane {
                        SplitView.minimumHeight: 200
                        SplitView.preferredHeight: 320
                        viewModel: mainViewModel
                        formatTimestamp: formatArticleTimestamp
                        showOpenButton: true
                        onOpenInBrowserRequested: mainViewModel.openSelectedArticle()
                    }

                    ArticlePreviewPane {
                        SplitView.minimumHeight: 220
                        viewModel: mainViewModel
                    }
                }
            }

            StackLayout {
                anchors.fill: parent
                visible: narrowLayout
                currentIndex: narrowPageIndex

                FeedPane {
                    viewModel: mainViewModel
                    narrowMode: true
                    onNodeActivated: function(nodeId, nodeType, synthetic, hasChildren) {
                        if (synthetic || nodeType === 1) {
                            openNarrowArticles(nodeId)
                        } else if (hasChildren) {
                            mainViewModel.selectNode(nodeId)
                            mainViewModel.toggleExpanded(nodeId)
                        } else {
                            mainViewModel.selectNode(nodeId)
                        }
                    }
                    onNodeSelected: function(nodeId) {
                        mainViewModel.selectNode(nodeId)
                    }
                    onNodeExpansionRequested: function(nodeId) {
                        mainViewModel.toggleExpanded(nodeId)
                    }
                    onAddFolderRequested: function(nodeId) {
                        openAddFolderDialog(nodeId)
                    }
                    onAddFeedRequested: function(nodeId) {
                        openAddFeedDialog(nodeId)
                    }
                    onRefreshRequested: function(nodeId) {
                        mainViewModel.refreshFeed(nodeId)
                    }
                    onMarkAllReadRequested: function(nodeId) {
                        mainViewModel.markAllArticlesRead(nodeId)
                    }
                    onConfigureRequested: function(nodeId) {
                        openConfigureDialog(nodeId)
                    }
                    onRenameRequested: function(nodeId) {
                        openRenameDialog(nodeId)
                    }
                    onDeleteRequested: function(nodeId) {
                        openDeleteDialog(nodeId)
                    }
                }

                ArticleListPane {
                    viewModel: mainViewModel
                    narrowMode: true
                    showBackButton: true
                    showPreviewButton: true
                    showOpenButton: true
                    titleText: selectedNodeTitle()
                    formatTimestamp: formatArticleTimestamp
                    onBackRequested: narrowPageIndex = 0
                    onPreviewRequested: narrowPageIndex = 2
                    onOpenInBrowserRequested: mainViewModel.openSelectedArticle()
                    onArticlePreviewRequested: function(row) {
                        openNarrowPreview(row)
                    }
                }

                ArticlePreviewPane {
                    viewModel: mainViewModel
                    showBackButton: true
                    showOpenButton: true
                    titleText: qsTr("Preview")
                    emptySubtitle: qsTr("Long-press an article or use the Preview button to open it here.")
                    onBackRequested: narrowPageIndex = 1
                    onOpenInBrowserRequested: mainViewModel.openSelectedArticle()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            color: mainViewModel.statusBarText.length > 0 ? statusColor : "#dde3e6"
            visible: true

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 12
                elide: Text.ElideRight
                text: mainViewModel.statusBarText.length > 0
                      ? (mainViewModel.hasStatusBarDetail
                         ? qsTr("%1  Double-click or long-press for details.").arg(mainViewModel.statusBarText)
                         : mainViewModel.statusBarText)
                      : qsTr("Ready.")
                color: mainViewModel.statusBarText.length > 0 ? "white" : "#263238"
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onDoubleClicked: showStatusDetails()
                onPressAndHold: showStatusDetails()
            }
        }
    }
}

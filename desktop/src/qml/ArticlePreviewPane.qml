import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root

    required property var viewModel
    property bool showBackButton: false
    property bool showOpenButton: false
    property string titleText: qsTr("Article Preview")
    property string emptySubtitle: qsTr("Select an article to see its sanitized content preview here.")

    signal backRequested()
    signal openInBrowserRequested()

    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

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

            Button {
                visible: root.showOpenButton
                text: qsTr("Open in Browser")
                enabled: !!root.viewModel && root.viewModel.hasPreview
                onClicked: root.openInBrowserRequested()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 460)
                spacing: 10
                visible: !root.viewModel || !root.viewModel.hasPreview

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Nothing to preview yet.")
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

            ScrollView {
                id: previewScroll
                anchors.fill: parent
                clip: true
                visible: !!root.viewModel && root.viewModel.hasPreview
                contentWidth: availableWidth

                background: Rectangle {
                    color: "white"
                    radius: 6
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AlwaysOn
                }

                ScrollBar.horizontal: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                Column {
                    width: previewScroll.availableWidth
                    spacing: 10
                    padding: 14

                    TextArea {
                        width: parent.width
                        text: root.viewModel ? root.viewModel.previewTitle : ""
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        font.pixelSize: 24
                        font.bold: true
                        textFormat: TextEdit.PlainText
                        background: null
                        padding: 0
                    }

                    TextArea {
                        width: parent.width
                        text: root.viewModel ? root.viewModel.previewMeta : ""
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        opacity: 0.65
                        textFormat: TextEdit.PlainText
                        background: null
                        padding: 0
                    }

                    TextArea {
                        width: parent.width
                        text: root.viewModel ? root.viewModel.previewContent : ""
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.RichText
                        background: null
                        padding: 0
                    }
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls

Label {
    id: root

    property int iconSize: 22

    FontLoader {
        id: materialSymbols
        source: "qrc:/qt/qml/OneRSS/fonts/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf"
    }

    font.family: materialSymbols.name
    font.pixelSize: iconSize
    font.weight: Font.Normal
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    renderType: Text.QtRendering
}

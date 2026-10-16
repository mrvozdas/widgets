import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: flyout
    property string heading
    default property alias body: bodyColumn.data
    popupType: Popup.Window
    property point anchorPoint: Qt.point(0, 0)
    property rect workArea: Qt.rect(0, 0, 800, 600)
    width: Math.min(348, Math.max(1, workArea.width - 16))
    readonly property real naturalHeight: header.implicitHeight + bodyColumn.implicitHeight + 56
    implicitHeight: Math.min(naturalHeight, Math.max(1, workArea.height - 16))
    height: implicitHeight
    x: Math.max(workArea.x + 8, Math.min(anchorPoint.x + ((parent ? parent.width : 0) - width) / 2,
                                        workArea.x + workArea.width - width - 8)) - anchorPoint.x
    y: Math.max(workArea.y + 8, Math.min(anchorPoint.y - height - 8 >= workArea.y
                 ? anchorPoint.y - height - 8 : anchorPoint.y + (parent ? parent.height : 0) + 8,
                 workArea.y + workArea.height - height - 8)) - anchorPoint.y
    padding: 20
    margins: 8
    focus: true
    modal: false
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    font.family: "Segoe UI Variable"
    font.pixelSize: 14

    // Popup.Window maps the anchor through its window and clips to the screen.
    // An item popup would be clipped by the 32-DIP taskbar overlay.
    function openFor(anchor) {
        parent = anchor
        anchorPoint = anchor.mapToGlobal(0, 0)
        workArea = Screens.availableAt(anchorPoint.x, anchorPoint.y)
        open()
    }

    Connections {
        id: ownerVisibility
        target: flyout.parent ? flyout.parent.Window.window : null
        function onVisibleChanged() {
            if (ownerVisibility.target && !ownerVisibility.target.visible) flyout.close()
        }
    }
    background: Rectangle {
        radius: 8
        color: flyout.palette.window
        border.color: flyout.palette.mid
        border.width: 1
    }
    contentItem: Column {
        spacing: 16
        RowLayout {
            id: header
            width: parent.width
            Label {
                text: flyout.heading
                textFormat: Text.PlainText
                font.pixelSize: 18
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            ToolButton {
                text: "×"
                Accessible.name: "Kapat"
                onClicked: flyout.close()
            }
        }
        ScrollView {
            id: scroll
            width: parent.width
            height: Math.max(0, flyout.availableHeight - header.implicitHeight - 16)
            contentWidth: availableWidth
            clip: true
            Column {
                id: bodyColumn
                width: scroll.availableWidth
                spacing: 10
            }
        }
    }
}

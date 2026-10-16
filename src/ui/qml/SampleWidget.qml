import QtQuick

Rectangle {
    id: sample

    property var widgetModel
    property var requestPanelSettings

    radius: 6
    color: interaction.containsMouse ? "#343945" : "#2a2e37"
    clip: true

    Rectangle {
        id: accentBadge

        x: 7
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        radius: 3
        color: sample.widgetModel ? sample.widgetModel.accent : "#8ddbc2"
    }

    Text {
        anchors.left: accentBadge.right
        anchors.leftMargin: 6
        anchors.right: countBadge.left
        anchors.rightMargin: 5
        anchors.verticalCenter: parent.verticalCenter
        text: sample.widgetModel ? sample.widgetModel.label : "Örnek"
        color: "#eef1f4"
        font.family: "Segoe UI"
        font.pixelSize: 11
        elide: Text.ElideRight
    }

    Text {
        id: countBadge

        anchors.right: parent.right
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        text: sample.widgetModel ? sample.widgetModel.counter.toString() : "0"
        color: sample.widgetModel ? sample.widgetModel.accent : "#8ddbc2"
        font.family: "Segoe UI"
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }

    MouseArea {
        id: interaction

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (sample.widgetModel && sample.widgetModel.active)
                sample.widgetModel.trigger()
        }
    }
}

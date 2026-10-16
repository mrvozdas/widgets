import QtQuick
import QtQuick.Controls

Rectangle {
    id: worldClock
    objectName: "worldClockWidget"

    property var widgetModel
    property string widgetInstanceId
    property var requestPanelSettings
    readonly property var reading: widgetModel && widgetModel.clocks.length > 0
                                   ? widgetModel.clocks[0]
                                   : ({ abbreviation: "", date: "", time: "--:--" })
    readonly property string displayedTime: widgetModel && widgetModel.showDate
                                           ? reading.date.slice(5) + " " + reading.time : reading.time
    readonly property string displayedName: widgetModel && widgetModel.displayName
                                           ? widgetModel.displayName : reading.abbreviation
    readonly property font readingFont: Qt.font({ family: "Segoe UI", pixelSize: 11,
                                                  weight: Font.DemiBold })
    readonly property real labelGap: zoneMetrics.advanceWidth > 0 ? spaceMetrics.advanceWidth : 0

    implicitWidth: Math.ceil(zoneMetrics.advanceWidth + labelGap + timeMetrics.advanceWidth) + 14
    radius: 6
    color: interaction.containsMouse ? "#343945" : "#2a2e37"
    clip: true
    Accessible.role: Accessible.Button
    Accessible.name: displayedName + " " + displayedTime
    Accessible.description: I18n.text("ui.saat_ayrintilarini_goster")
    Accessible.onPressAction: showDetails()

    function showDetails() {
        if (widgetModel && widgetModel.active)
            details.openFor(worldClock)
    }

    TextMetrics {
        id: timeMetrics
        font: worldClock.readingFont
        text: worldClock.displayedTime
    }

    TextMetrics {
        id: zoneMetrics
        font: worldClock.readingFont
        text: worldClock.displayedName
    }

    TextMetrics {
        id: spaceMetrics
        font: worldClock.readingFont
        text: "  "
    }

    Row {
        id: clockRow
        objectName: "worldClockRow"
        anchors.left: parent.left
        anchors.leftMargin: 7
        height: parent.height

        Item {
            id: clockReading
            objectName: "worldClockReading"
            width: zoneMetrics.advanceWidth + worldClock.labelGap + timeMetrics.advanceWidth
            height: worldClock.height

            Text {
                id: clockZone
                objectName: "worldClockZone"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: zoneMetrics.advanceWidth
                text: worldClock.displayedName
                textFormat: Text.PlainText
                color: "#eef1f4"
                font: worldClock.readingFont
            }

            Text {
                objectName: "worldClockTime"
                anchors.left: clockZone.right
                anchors.leftMargin: worldClock.labelGap
                anchors.verticalCenter: parent.verticalCenter
                width: timeMetrics.advanceWidth
                text: worldClock.displayedTime
                color: "#eef1f4"
                font: worldClock.readingFont
            }
        }
    }

    MouseArea {
        id: interaction
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: worldClock.showDetails()
    }
    WidgetFlyout {
        id: details
        objectName: "worldClockFlyout"
        heading: worldClock.displayedName
        Label {
            text: worldClock.reading.time
            font.pixelSize: 36
            font.weight: Font.DemiBold
        }
        Label {
            width: parent.width
            text: worldClock.widgetModel ? worldClock.widgetModel.details : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        Button {
            text: I18n.text("ui.widget_ayarlari")
            onClicked: { details.close(); worldClock.requestPanelSettings() }
        }
    }
    Connections {
        target: worldClock.widgetModel
        function onActiveChanged() {
            if (!worldClock.widgetModel.active) details.close()
        }
    }
}

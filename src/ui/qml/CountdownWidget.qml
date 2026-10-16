import QtQuick
import QtQuick.Controls
import QtQuick.Window

Rectangle {
    id: countdown
    objectName: "countdownWidget"

    property var widgetModel
    property string widgetInstanceId
    property var requestPanelSettings
    property bool compact: widgetModel && width < widgetModel.compactWidthDip
    // The taskbar slot follows the rendered timer, independently of its saved
    // manager width. Long hour values and completion text can still grow to fit.
    implicitWidth: Math.ceil(countdownDisplay.implicitWidth) + 12

    function resetEditor() {
        nameField.text = widgetModel ? widgetModel.label : ""
        targetField.text = widgetModel ? widgetModel.targetTime : "18:00"
        repeatCheck.checked = widgetModel ? widgetModel.dailyRepeat : true
        zoneField.text = widgetModel ? widgetModel.timeZoneId : "Europe/Istanbul"
        errorLabel.text = ""
    }

    function saveEditor() {
        if (!countdown.widgetModel || !widgetHostModel) return
        const normalized = countdown.widgetModel.normalizedSettingsMap({
            "label": nameField.text,
            "target_time": targetField.text,
            "daily_repeat": repeatCheck.checked,
            "time_zone": zoneField.text,
            "target_date": countdown.widgetModel.targetDate
        })
        if (Object.keys(normalized).length === 0) {
            errorLabel.text = countdown.widgetModel.lastValidationError
            return
        }
        const error = widgetHostModel.updateWidgetSettings(countdown.widgetInstanceId, normalized)
        if (error.length > 0) {
            errorLabel.text = error
            return
        }
        editor.visible = false
    }

    radius: 6
    color: interaction.containsMouse ? "#343945" : "#2a2e37"
    clip: true

    Row {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        spacing: 5

        Text {
            id: countdownDisplay
            objectName: "countdownDisplay"
            anchors.verticalCenter: parent.verticalCenter
            text: countdown.widgetModel ? countdown.widgetModel.displayText : "⏳ --:--"
            color: countdown.widgetModel && countdown.widgetModel.status === "completed"
                   ? "#8ddbc2" : "#eef1f4"
            font.family: "Segoe UI"
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        id: interaction
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (countdown.widgetModel && countdown.widgetModel.active) {
                countdown.resetEditor()
                editor.visible = true
            }
        }
    }

    Window {
        id: editor

        transientParent: countdown.Window.window
        flags: Qt.Tool | Qt.FramelessWindowHint
        modality: Qt.NonModal
        visible: false
        x: transientParent
           ? transientParent.x + Math.max(0, Math.min(countdown.x, transientParent.width - width))
           : 0
        y: transientParent
           ? (transientParent.y >= height + 6
              ? transientParent.y - height - 6
              : transientParent.y + transientParent.height + 6)
           : 0
        width: 260
        height: settingsContent.implicitHeight + 20
        color: "transparent"

        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "#252a33"
            border.color: "#596574"
            border.width: 1

            Column {
                id: settingsContent

                anchors.fill: parent
                anchors.margins: 10
                spacing: 7

                Label { text: "Geri sayım ayarları"; color: "#eef1f4"; font.bold: true }
                TextField {
                    id: nameField
                    width: parent.width
                    placeholderText: "Widget adı"
                    maximumLength: 40
                    onAccepted: countdown.saveEditor()
                }
                TextField {
                    id: targetField
                    width: parent.width
                    placeholderText: "Hedef saat (HH:mm)"
                    maximumLength: 5
                    selectByMouse: true
                    onAccepted: countdown.saveEditor()
                    onTextEdited: {
                        const digitCountBeforeCursor =
                                text.slice(0, cursorPosition).replace(/[^0-9]/g, "").length
                        const digits = text.replace(/[^0-9]/g, "").slice(0, 4)
                        const formatted = digits.length >= 2
                                          ? digits.slice(0, 2) + ":" + digits.slice(2)
                                          : digits
                        if (text !== formatted) {
                            text = formatted
                            cursorPosition = digitCountBeforeCursor < 2
                                             ? digitCountBeforeCursor
                                             : Math.min(digitCountBeforeCursor + 1, formatted.length)
                        }
                    }
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Backspace && cursorPosition === 3) {
                            cursorPosition = 2
                            event.accepted = true
                        } else if (event.key === Qt.Key_Delete && cursorPosition === 2) {
                            cursorPosition = 3
                            event.accepted = true
                        }
                    }
                }
                CheckBox { id: repeatCheck; text: "Günlük tekrar"; checked: true }
                TextField {
                    id: zoneField
                    width: parent.width
                    placeholderText: "IANA saat dilimi"
                    text: "Europe/Istanbul"
                    onAccepted: countdown.saveEditor()
                }
                Label {
                    id: errorLabel
                    width: parent.width
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    color: "#ffc7c0"
                }
                Row {
                spacing: 8
                    Button {
                        text: "Kaydet"
                        onClicked: countdown.saveEditor()
                    }
                    Button {
                        text: "İptal"
                        onClicked: editor.visible = false
                    }
                }
            }
        }
    }
}

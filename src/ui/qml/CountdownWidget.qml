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
        dateField.text = widgetModel ? widgetModel.targetDate : ""
        errorLabel.text = ""
    }

    function saveEditor() {
        if (!countdown.widgetModel || !widgetHostModel) return
        const normalized = countdown.widgetModel.normalizedSettingsMap({
            "label": nameField.text,
            "target_time": targetField.text,
            "daily_repeat": repeatCheck.checked,
            "time_zone": zoneField.text,
            "target_date": repeatCheck.checked ? "" : dateField.text
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
        editor.close()
    }

    Accessible.role: Accessible.Button
    Accessible.name: widgetModel ? widgetModel.label + " " + widgetModel.displayText : I18n.text("ui.geri_sayim")
    Accessible.onPressAction: { resetEditor(); editor.openFor(countdown) }

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
                editor.openFor(countdown)
            }
        }
    }

    WidgetFlyout {
        id: editor
        objectName: "countdownFlyout"
        heading: I18n.text("ui.geri_sayim")
        Label {
            text: countdown.widgetModel ? countdown.widgetModel.displayText : ""
            font.pixelSize: 30
            font.weight: Font.DemiBold
        }
        Label { text: I18n.text("ui.ad") }
        TextField {
            id: nameField
            width: parent.width
            maximumLength: 40
            Accessible.name: I18n.text("ui.widget_adi")
            onAccepted: countdown.saveEditor()
        }
        Label { text: I18n.text("ui.hedef_saat") }
        TextField {
            id: targetField
            width: parent.width
            placeholderText: "HH:mm"
            maximumLength: 5
            selectByMouse: true
            Accessible.name: I18n.text("ui.hedef_saat")
            onAccepted: countdown.saveEditor()
        }
        CheckBox { id: repeatCheck; text: I18n.text("ui.her_gun_tekrarla") }
        Label { text: I18n.text("ui.hedef_tarih"); visible: !repeatCheck.checked }
        TextField {
            id: dateField
            width: parent.width
            visible: !repeatCheck.checked
            placeholderText: "YYYY-MM-DD"
            Accessible.name: I18n.text("ui.hedef_tarih")
            maximumLength: 10
            selectByMouse: true
        }
        Label {
            width: parent.width
            visible: !repeatCheck.checked
            text: I18n.text("ui.tarihi_bos_birakirsaniz_hedef_saatin_bir_sonraki_gercekl")
            wrapMode: Text.Wrap
            opacity: 0.75
        }
        Label { text: I18n.text("ui.saat_dilimi") }
        TextField {
            id: zoneField
            width: parent.width
            placeholderText: "Europe/Istanbul"
            Accessible.name: I18n.text("ui.iana_saat_dilimi")
            selectByMouse: true
            onAccepted: countdown.saveEditor()
        }
        Label {
            id: errorLabel
            width: parent.width
            visible: text.length > 0
            wrapMode: Text.Wrap
            color: "#c42b1c"
            Accessible.role: Accessible.AlertMessage
        }
        Row {
            spacing: 8
            Button { text: I18n.text("ui.kaydet"); highlighted: true; onClicked: countdown.saveEditor() }
            Button { text: I18n.text("ui.iptal"); onClicked: editor.close() }
        }
    }
    Connections {
        target: countdown.widgetModel
        function onActiveChanged() {
            if (!countdown.widgetModel.active) editor.close()
        }
    }
}

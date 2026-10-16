import QtQuick
import QtQuick.Controls

Rectangle {
    id: focusTimer
    objectName: "focusTimerWidget"
    property var widgetModel
    property string widgetInstanceId
    property var requestPanelSettings
    implicitWidth: timerText.implicitWidth + 14
    radius: 6
    color: interaction.containsMouse ? "#343945" : "#2a2e37"
    Accessible.role: Accessible.Button
    Accessible.name: timerText.text
    Accessible.onPressAction: details.openFor(focusTimer)

    Text {
        id: timerText
        anchors.centerIn: parent
        text: widgetModel ? widgetModel.label + "  " + widgetModel.displayText : "--:--"
        color: widgetModel && widgetModel.running ? "#99ebff" : "#eef1f4"
        font.family: "Segoe UI Variable"
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
    MouseArea {
        id: interaction
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: details.openFor(focusTimer)
    }
    WidgetFlyout {
        id: details
        objectName: "focusTimerFlyout"
        heading: focusTimer.widgetModel ? focusTimer.widgetModel.label : I18n.text("ui.odak")
        Label {
            text: focusTimer.widgetModel ? focusTimer.widgetModel.displayText : "--:--"
            font.pixelSize: 40
            font.weight: Font.DemiBold
        }
        Label {
            width: parent.width
            text: !focusTimer.widgetModel ? ""
                : focusTimer.widgetModel.status === "completed" ? I18n.text("ui.sure_tamamlandi_mola_verebilirsiniz")
                : focusTimer.widgetModel.running ? I18n.text("ui.odak_oturumu_suruyor")
                : I18n.text("ui.hazir_oldugunuzda_baslatin")
            wrapMode: Text.Wrap
        }
        Row {
            spacing: 8
            Button {
                text: focusTimer.widgetModel && focusTimer.widgetModel.running ? I18n.text("ui.duraklat") : I18n.text("ui.baslat")
                highlighted: true
                onClicked: {
                    if (!focusTimer.widgetModel) return
                    if (focusTimer.widgetModel.running) focusTimer.widgetModel.pause()
                    else focusTimer.widgetModel.start()
                }
            }
            Button { text: I18n.text("ui.sifirla"); onClicked: focusTimer.widgetModel.reset() }
        }
        Label { text: I18n.text("ui.oturum_suresi_dakika") }
        SpinBox {
            id: duration
            from: 1
            to: 180
            value: focusTimer.widgetModel ? focusTimer.widgetModel.durationMinutes : 25
            Accessible.name: I18n.text("ui.oturum_suresi_dakika")
            editable: true
        }
        Button {
            text: I18n.text("ui.sureyi_uygula_ve_sifirla")
            onClicked: {
                errorText.text = widgetHostModel.updateWidgetSettings(focusTimer.widgetInstanceId,
                    {"label": focusTimer.widgetModel.label, "duration_minutes": duration.value})
                if (errorText.text.length === 0) focusTimer.widgetModel.reset()
            }
        }
        Label {
            id: errorText
            width: parent.width
            visible: text.length > 0
            wrapMode: Text.Wrap
            color: "#c42b1c"
        }
        Label {
            width: parent.width
            text: I18n.text("ui.panel_veya_widget_gizlendiginde_oturum_duraklar_uygulama")
            wrapMode: Text.Wrap
            opacity: 0.75
        }
    }
    Connections {
        target: focusTimer.widgetModel
        function onActiveChanged() {
            if (!focusTimer.widgetModel.active) details.close()
        }
    }
}

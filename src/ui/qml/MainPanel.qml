import QtQuick

Rectangle {
    id: panel

    width: 330
    height: 32
    radius: 10
    color: "#23252c"
    border.width: 1
    border.color: "#505965"
    clip: true

    property int visibleModuleCount: 0
    property int visibleModuleWidth: 0
    property int fixedModuleWidth: 0
    property int flexibleModuleWidth: 0
    property real slotScale: 1
    signal settingsRequested()

    function updateLayout() {
        let total = 0
        let totalWidth = 0
        let fixedWidth = 0
        let flexibleWidth = 0
        for (let index = 0; index < modules.count; ++index) {
            const item = modules.itemAt(index)
            if (item && item.moduleEnabled && item.moduleVisible) {
                ++total
                totalWidth += item.widthDip
                if (item.fixedSize)
                    fixedWidth += item.naturalWidth
                else
                    flexibleWidth += item.widthDip
            }
        }
        visibleModuleCount = total
        visibleModuleWidth = totalWidth
        const available = Math.max(0, viewport.width - Math.max(0, total - 1) * moduleRow.spacing)
        fixedModuleWidth = fixedWidth
        flexibleModuleWidth = flexibleWidth
        slotScale = flexibleWidth > 0 ? Math.max(0, available - fixedWidth) / flexibleWidth : 1
    }

    Flickable {
        id: viewport
        objectName: "panelWidgetViewport"

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 3
        anchors.rightMargin: panelSettingsButton.width + 10
        clip: true
        contentWidth: moduleRow.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        onWidthChanged: Qt.callLater(panel.updateLayout)

        Row {
            id: moduleRow

            height: viewport.height
            spacing: 4

            component ModuleSlot: Item {
                id: moduleSlot

                required property string instanceId
                required property string typeId
                required property bool moduleEnabled
                required property bool moduleVisible
                required property int widthDip
                required property string componentSource
                required property var viewModel
                required property string errorMessage
                required property bool contentSized
                property bool fixedSize: contentSized
                readonly property real naturalWidth: fixedSize
                    ? Math.ceil(moduleLoader.item && moduleLoader.item.implicitWidth > 0
                                ? moduleLoader.item.implicitWidth : 72)
                    : widthDip

                width: moduleEnabled && moduleVisible
                       ? fixedSize ? naturalWidth : Math.max(48, Math.floor(widthDip * panel.slotScale)) : 0
                height: moduleRow.height
                visible: moduleEnabled && moduleVisible
                clip: true
                property bool componentFailed: false

                onModuleEnabledChanged: Qt.callLater(panel.updateLayout)
                onModuleVisibleChanged: Qt.callLater(panel.updateLayout)
                onWidthDipChanged: Qt.callLater(panel.updateLayout)
                onNaturalWidthChanged: Qt.callLater(panel.updateLayout)

                Loader {
                    id: moduleLoader

                    anchors.fill: parent
                    active: moduleSlot.visible && moduleSlot.errorMessage.length === 0

                    function loadModule() {
                        moduleSlot.componentFailed = false
                        if (!active || !moduleSlot.componentSource || !moduleSlot.viewModel) {
                            source = ""
                            return
                        }
                        setSource(moduleSlot.componentSource,
                                  { "widgetModel": moduleSlot.viewModel,
                                    "widgetInstanceId": moduleSlot.instanceId,
                                    "requestPanelSettings": panel.settingsRequested })
                    }

                    onActiveChanged: loadModule()
                    onStatusChanged: {
                        if (status === Loader.Error)
                            moduleSlot.componentFailed = true
                    }
                    Component.onCompleted: loadModule()

                    Connections {
                        target: moduleSlot
                        function onComponentSourceChanged() { moduleLoader.loadModule() }
                        function onViewModelChanged() { moduleLoader.loadModule() }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: "#442f32"
                    visible: moduleSlot.errorMessage.length > 0 || moduleSlot.componentFailed

                    Text {
                        anchors.fill: parent
                        anchors.margins: 5
                        text: moduleSlot.errorMessage.length > 0
                              ? moduleSlot.errorMessage : I18n.text("ui.modul_yuklenemedi")
                        color: "#ffc7c0"
                        font.family: "Segoe UI Variable"
                        font.pixelSize: 10
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }

            Repeater {
                id: modules

                model: widgetHostModel
                onItemAdded: Qt.callLater(panel.updateLayout)
                onItemRemoved: Qt.callLater(panel.updateLayout)

                delegate: ModuleSlot { }
            }
        }
    }

    Rectangle {
        id: panelSettingsButton
        objectName: "worldClockSettingsButton"

        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 24
        height: 24
        radius: 5
        color: settingsInteraction.pressed ? "#47515f"
                                          : settingsInteraction.containsMouse ? "#38424f" : "#2b323d"
        border.width: 1
        border.color: "#596574"
        Accessible.role: Accessible.Button
        Accessible.name: "Ayarlar"
        Accessible.description: I18n.text("ui.widget_ayarlarini_ac")
        Accessible.onPressAction: panelSettingsButton.activate()

        function activate() { panel.settingsRequested() }

        Text {
            anchors.centerIn: parent
            text: "\u2699"
            color: "#eef1f4"
            font.family: "Segoe UI Symbol"
            font.pixelSize: 13
        }

        MouseArea {
            id: settingsInteraction
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: panelSettingsButton.activate()
        }
    }

    Text {
        objectName: "panelEmptyLabel"
        anchors.centerIn: viewport
        visible: panel.visibleModuleCount === 0
        text: I18n.text("ui.modul_yok_moduller_bolumunden_ekleyebilirsin")
        width: Math.max(0, viewport.width - 8)
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: "#bfc7d1"
        font.family: "Segoe UI Variable"
        font.pixelSize: 11
    }
}

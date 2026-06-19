import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    visible: true
    width: Screen.width
    height: Screen.height
    title: "SPS - Smart Presentation System"
    visibility: Window.FullScreen
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    property bool bIsLocked: true
    property string szTeacherName: ""
    property string szCurrentScenario: ""
    property string szMqttStatus: "DISCONNECTED"
    property string szAuthStatus: "UNKNOWN"
    property bool bNetworkConnected: dbusClient.networkConnected
    property bool bPcControlEnabled: dbusClient.pcControlEnabled
    property string szPcMacAddress: dbusClient.pcMacAddress
    property var scenarioIds: []
    property string szFooterStatus: "Ready"

    function refreshScenarios() {
        var scenarios = dbusClient.getAvailableScenarios()
        scenarioIds = scenarios ? scenarios : []
    }

    function displayScenarioName(scenarioId) {
        return dbusClient.getScenarioDisplayName(scenarioId)
    }

    function scenarioColumnCount() {
        if (dashboard.width < 760)
            return 1
        if (dashboard.width < 1120)
            return 2
        return 3
    }

    function deviceColumnCount() {
        if (dashboard.width < 760)
            return 1
        if (dashboard.width < 1120)
            return 2
        return 3
    }

    function controlDevice(deviceKey, action) {
        console.log("[QML]: Device control: " + deviceKey + " -> " + action)
        dbusClient.controlClassroomDevice(deviceKey, action)
    }

    Connections {
        target: dbusClient
        function onAuthStatusChanged(status) {
            console.log("[QML]: Auth status changed: " + status)
            szAuthStatus = status
            if (status === "UNLOCKED") {
                var lecturer = dbusClient.getAuthenticatedLecturer()
                if (lecturer.length > 0) {
                    szTeacherName = lecturer
                }
                bIsLocked = false
            }
        }
        function onLecturerAuthenticated(lecturerName, timestamp) {
            console.log("[QML]: Lecturer authenticated: " + lecturerName)
            szTeacherName = lecturerName
            bIsLocked = false
        }
        function onMqttStatusChanged(status) {
            console.log("[QML]: MQTT status: " + status)
            szMqttStatus = status
            bNetworkConnected = (status === "CONNECTED")
        }
        function onNetworkStatusChanged(connected) {
            console.log("[QML]: Network connected: " + connected)
            bNetworkConnected = connected
        }
        function onPcControlConfigChanged() {
            bPcControlEnabled = dbusClient.pcControlEnabled
            szPcMacAddress = dbusClient.pcMacAddress
        }
        function onScenariosUpdated(scenarios) {
            scenarioIds = scenarios
        }
        function onScenarioStarted(scenarioId) {
            console.log("[QML]: Scenario started: " + scenarioId)
            szCurrentScenario = scenarioId
            szFooterStatus = "Executing"
        }
        function onScenarioCompleted(scenarioId) {
            console.log("[QML]: Scenario completed: " + scenarioId)
            szFooterStatus = "Ready"
            szCurrentScenario = ""
        }
        function onScenarioError(scenarioId, error) {
            console.log("[QML]: Scenario error: " + error)
            szFooterStatus = "Error: " + error
            szCurrentScenario = ""
        }
    }

    Component.onCompleted: refreshScenarios()

    component StatusChip: Rectangle {
        property string label: ""
        property string tone: "neutral"

        implicitWidth: chipText.implicitWidth + 24
        implicitHeight: 34
        radius: 8
        color: tone === "good" ? "#e8f8ef"
              : tone === "danger" ? "#fdecec"
              : "#eef2f7"
        border.color: tone === "good" ? "#23a56f"
                    : tone === "danger" ? "#dc4c4c"
                    : "#c7d0dc"
        border.width: 1

        Text {
            id: chipText
            anchors.centerIn: parent
            text: label
            color: tone === "good" ? "#146c48"
                  : tone === "danger" ? "#b42323"
                  : "#334155"
            font.pixelSize: 14
            font.bold: true
        }
    }

    component ControlButton: Button {
        id: controlButton
        property string tone: "primary"

        Layout.fillWidth: true
        Layout.preferredHeight: 48
        font.pixelSize: 15
        font.bold: true

        background: Rectangle {
            radius: 6
            color: !controlButton.enabled ? "#e2e8f0"
                  : controlButton.down ? "#0f766e"
                  : tone === "primary" ? "#0f9f8f"
                  : tone === "danger" ? "#475569"
                  : tone === "stop" ? "#f1f5f9"
                  : "#ffffff"
            border.color: !controlButton.enabled ? "#cbd5e1"
                        : tone === "primary" ? "#0f766e"
                        : tone === "danger" ? "#334155"
                        : tone === "stop" ? "#94a3b8"
                        : "#cbd5e1"
            border.width: 1
        }

        contentItem: Text {
            text: controlButton.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: !controlButton.enabled ? "#94a3b8"
                  : controlButton.tone === "stop" ? "#334155"
                  : controlButton.tone === "secondary" ? "#334155"
                  : "#ffffff"
            font: controlButton.font
            elide: Text.ElideRight
        }
    }

    component ScenarioButton: Button {
        id: scenarioButton
        Layout.fillWidth: true
        Layout.preferredHeight: 58
        font.pixelSize: 16
        font.bold: true

        background: Rectangle {
            radius: 8
            color: scenarioButton.down ? "#0f766e" : "#ffffff"
            border.color: "#0f9f8f"
            border.width: 1
        }

        contentItem: Text {
            text: scenarioButton.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: scenarioButton.down ? "#ffffff" : "#0f766e"
            font: scenarioButton.font
            elide: Text.ElideRight
        }
    }

    component DeviceCard: Rectangle {
        property string title: ""
        property string subtitle: ""
        property int actionColumns: 2
        default property alias actionItems: actionRow.data

        Layout.fillWidth: true
        Layout.preferredHeight: 88 + Math.ceil(actionRow.children.length / actionColumns) * 56
        radius: 8
        color: "#ffffff"
        border.color: "#d8dee8"
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: title
                    color: "#172033"
                    font.pixelSize: 17
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: subtitle
                    color: "#64748b"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            GridLayout {
                id: actionRow
                columns: actionColumns
                Layout.fillWidth: true
                columnSpacing: 8
                rowSpacing: 8
            }
        }
    }

    //
    // LOCK SCREEN
    //
    Rectangle {
        id: lockScreen
        anchors.fill: parent
        color: "#1e1e1e"
        visible: bIsLocked

        Image {
            id: backgroundImage
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            opacity: 0.3
            source: "" // Could load a background image
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 40
            width: parent.width * 0.8

            Text {
                text: "SMART PRESENTATION SYSTEM"
                color: "white"
                font.pixelSize: 48
                font.bold: true
                font.family: "Arial"
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: "Classroom Control Interface"
                color: "#00bfff"
                font.pixelSize: 32
                Layout.alignment: Qt.AlignHCenter
            }

            Rectangle {
                width: 300
                height: 200
                color: "#2a2a2a"
                border.color: "#00bfff"
                border.width: 3
                radius: 10
                Layout.alignment: Qt.AlignHCenter

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 15

                    Text {
                        text: "RFID Scanner Ready"
                        color: "white"
                        font.pixelSize: 24
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "Please swipe your ID card"
                        color: "#cccccc"
                        font.pixelSize: 18
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        id: rfidStatus
                        text: "Waiting for card..."
                        color: "#00bfff"
                        font.pixelSize: 16
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 20

                Text {
                    text: "Network: " + (bNetworkConnected ? "Connected" : "Offline")
                    color: bNetworkConnected ? "#00ff00" : "#ff6b6b"
                    font.pixelSize: 14
                }

                Text {
                    text: "Auth: " + szAuthStatus
                    color: "#00bfff"
                    font.pixelSize: 14
                }
            }

            Button {
                text: "TEST LOGIN (Demo)"
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: 18
                width: 200
                height: 60
                onClicked: {
                    console.log("[QML]: Test login clicked")
                    dbusClient.unlockScreen("RFID001")
                }
            }
        }
    }

    //
    // MAIN DASHBOARD
    //
    Rectangle {
        id: dashboard
        anchors.fill: parent
        color: "#f6f8fb"
        visible: !bIsLocked

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: dashboard.width < 760 ? 12 : 18
            spacing: dashboard.width < 760 ? 10 : 14

            Rectangle {
                id: header
                Layout.fillWidth: true
                Layout.preferredHeight: dashboard.width < 760 ? 78 : 72
                radius: 8
                color: "#ffffff"
                border.color: "#d8dee8"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: dashboard.width < 760 ? 10 : 14
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: "SPS Classroom Control"
                            color: "#172033"
                            font.pixelSize: dashboard.width < 760 ? 20 : 24
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: "Welcome, " + (szTeacherName.length > 0 ? szTeacherName.toUpperCase() : "LECTURER")
                            color: "#64748b"
                            font.pixelSize: dashboard.width < 760 ? 13 : 14
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    StatusChip {
                        label: "Network: " + (bNetworkConnected ? "Connected" : "Offline")
                        tone: bNetworkConnected ? "good" : "danger"
                        visible: dashboard.width >= 620
                    }

                    StatusChip {
                        label: "Unlocked"
                        tone: "good"
                        visible: dashboard.width >= 780
                    }

                    Button {
                        text: "Logout"
                        Layout.preferredWidth: 104
                        Layout.preferredHeight: 44
                        font.pixelSize: 15
                        font.bold: true
                        onClicked: {
                            console.log("[QML]: Logout clicked")
                            dbusClient.lockScreen()
                            szTeacherName = ""
                            szCurrentScenario = ""
                            szFooterStatus = "Ready"
                            bIsLocked = true
                        }
                    }
                }
            }

            ScrollView {
                id: dashboardScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: dashboardScroll.availableWidth
                    spacing: dashboard.width < 760 ? 12 : 16

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: scenarioIds.length === 0
                                                ? 96
                                                : 64 + Math.ceil(scenarioIds.length / scenarioColumnCount()) * 66
                        radius: 8
                        color: "#ffffff"
                        border.color: "#d8dee8"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Text {
                                    text: "Scenarios"
                                    color: "#172033"
                                    font.pixelSize: 20
                                    font.bold: true
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: scenarioIds.length + " available"
                                    color: "#64748b"
                                    font.pixelSize: 13
                                    visible: scenarioIds.length > 0
                                }
                            }

                            Text {
                                text: "No scenarios available"
                                color: "#64748b"
                                font.pixelSize: 15
                                visible: scenarioIds.length === 0
                                Layout.fillWidth: true
                            }

                            GridLayout {
                                id: scenarioGrid
                                columns: scenarioColumnCount()
                                Layout.fillWidth: true
                                columnSpacing: 10
                                rowSpacing: 10
                                visible: scenarioIds.length > 0

                                Repeater {
                                    model: scenarioIds

                                    ScenarioButton {
                                        text: displayScenarioName(modelData)
                                        enabled: szCurrentScenario.length === 0
                                        onClicked: {
                                            console.log("[QML]: Executing scenario: " + modelData)
                                            dbusClient.executeScenario(modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82 + Math.ceil(6 / deviceColumnCount()) * 144
                        radius: 8
                        color: "#ffffff"
                        border.color: "#d8dee8"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 12

                            Text {
                                text: "Device Control"
                                color: "#172033"
                                font.pixelSize: 20
                                font.bold: true
                                Layout.fillWidth: true
                            }

                            GridLayout {
                                columns: deviceColumnCount()
                                Layout.fillWidth: true
                                columnSpacing: 12
                                rowSpacing: 12

                                DeviceCard {
                                    title: "Desk PC"
                                    subtitle: bPcControlEnabled && szPcMacAddress.length > 0
                                              ? "Wake classroom computer"
                                              : "Wake disabled or MAC missing"

                                    ControlButton {
                                        text: "ON"
                                        tone: "primary"
                                        enabled: bPcControlEnabled && szPcMacAddress.length > 0
                                        onClicked: controlDevice("deskPc", "on")
                                    }

                                    ControlButton {
                                        text: "OFF"
                                        tone: "secondary"
                                        enabled: false
                                    }
                                }

                                DeviceCard {
                                    title: "Room Lights"
                                    subtitle: "Synchronized room lights"

                                    ControlButton {
                                        text: "ON"
                                        tone: "primary"
                                        onClicked: controlDevice("roomLights", "on")
                                    }

                                    ControlButton {
                                        text: "OFF"
                                        tone: "danger"
                                        onClicked: controlDevice("roomLights", "off")
                                    }
                                }

                                DeviceCard {
                                    title: "Curtains"
                                    subtitle: "All curtains synchronized"

                                    ControlButton {
                                        text: "OPEN"
                                        tone: "primary"
                                        onClicked: controlDevice("curtains", "open")
                                    }

                                    ControlButton {
                                        text: "CLOSE"
                                        tone: "danger"
                                        onClicked: controlDevice("curtains", "close")
                                    }
                                }

                                DeviceCard {
                                    title: "Projection Screen"
                                    subtitle: "Screen lift control"
                                    actionColumns: 3

                                    ControlButton {
                                        text: "UP"
                                        tone: "primary"
                                        onClicked: controlDevice("projectionScreen", "up")
                                    }

                                    ControlButton {
                                        text: "DOWN"
                                        tone: "danger"
                                        onClicked: controlDevice("projectionScreen", "down")
                                    }

                                    ControlButton {
                                        text: "STOP"
                                        tone: "stop"
                                        onClicked: controlDevice("projectionScreen", "stop")
                                    }
                                }

                                DeviceCard {
                                    title: "Projector"
                                    subtitle: "Temporary ON/OFF control"

                                    ControlButton {
                                        text: "ON"
                                        tone: "primary"
                                        onClicked: controlDevice("projector", "on")
                                    }

                                    ControlButton {
                                        text: "OFF"
                                        tone: "danger"
                                        onClicked: controlDevice("projector", "off")
                                    }
                                }

                                DeviceCard {
                                    title: "Air Conditioner"
                                    subtitle: "ON/OFF and temperature step control"

                                    ControlButton {
                                        text: "ON"
                                        tone: "primary"
                                        onClicked: controlDevice("airConditioner", "on")
                                    }

                                    ControlButton {
                                        text: "OFF"
                                        tone: "danger"
                                        onClicked: controlDevice("airConditioner", "off")
                                    }

                                    ControlButton {
                                        text: "TEMP -"
                                        tone: "stop"
                                        onClicked: controlDevice("airConditioner", "tempDown")
                                    }

                                    ControlButton {
                                        text: "TEMP +"
                                        tone: "primary"
                                        onClicked: controlDevice("airConditioner", "tempUp")
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                radius: 8
                color: "#ffffff"
                border.color: "#d8dee8"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Text {
                        text: szFooterStatus
                        color: szFooterStatus.indexOf("Error") === 0 ? "#b42323" : "#146c48"
                        font.pixelSize: 15
                        font.bold: true
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        color: "#d8dee8"
                    }

                    Text {
                        text: "Current scenario: " + (szCurrentScenario ? displayScenarioName(szCurrentScenario) : "None")
                        color: "#475569"
                        font.pixelSize: 14
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }

    // Prevent screen saver / sleep
    Timer {
        interval: 30000 // 30 seconds
        running: !bIsLocked
        repeat: true
        onTriggered: {
            // Keep system awake by monitoring mouse position
            // In production, use systemctl set-property or inhibit screensaver
        }
    }
}

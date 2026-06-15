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

    Connections {
        target: authBackend
        function onVLoginSuccess(uid, teacherName) {
            console.log("[QML]: Login success: " + teacherName)
            szTeacherName = teacherName
            bIsLocked = false
        }
    }

    Connections {
        target: dbusClient
        function onAuthStatusChanged(status) {
            console.log("[QML]: Auth status changed: " + status)
            szAuthStatus = status
        }
        function onMqttStatusChanged(status) {
            console.log("[QML]: MQTT status: " + status)
            szMqttStatus = status
        }
        function onScenarioStarted(scenarioId) {
            console.log("[QML]: Scenario started: " + scenarioId)
            szCurrentScenario = scenarioId
            statusText.text = "Executing: " + scenarioId
        }
        function onScenarioCompleted(scenarioId) {
            console.log("[QML]: Scenario completed: " + scenarioId)
            statusText.text = "Scenario completed!"
            szCurrentScenario = ""
        }
        function onScenarioError(scenarioId, error) {
            console.log("[QML]: Scenario error: " + error)
            statusText.text = "Error: " + error
            szCurrentScenario = ""
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
                    text: "Network: " + szMqttStatus
                    color: szMqttStatus === "CONNECTED" ? "#00ff00" : "#ff6b6b"
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
                    dbusClient.unlockScreen()
                    szTeacherName = "Demo Teacher"
                    bIsLocked = false
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
        color: "#f5f5f5"
        visible: !bIsLocked

        // Header
        Rectangle {
            id: header
            width: parent.width
            height: 80
            color: "#1e3a5f"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 20

                Text {
                    text: "Welcome, " + szTeacherName.toUpperCase()
                    color: "white"
                    font.pixelSize: 28
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "MQTT: " + szMqttStatus
                    color: szMqttStatus === "CONNECTED" ? "#00ff00" : "#ff6b6b"
                    font.pixelSize: 14
                }

                Button {
                    text: "Logout"
                    font.pixelSize: 16
                    width: 120
                    height: 50
                    onClicked: {
                        console.log("[QML]: Logout clicked")
                        dbusClient.lockScreen()
                        szTeacherName = ""
                        szCurrentScenario = ""
                        bIsLocked = true
                    }
                }
            }
        }

        ColumnLayout {
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 20
            spacing: 20

            // Scenarios Section
            GroupBox {
                title: "Quick Scenarios"
                Layout.fillWidth: true
                Layout.preferredHeight: 200

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    GridLayout {
                        columns: 3
                        Layout.fillWidth: true
                        columnSpacing: 10
                        rowSpacing: 10

                        Button {
                            text: "📺 Startup"
                            font.pixelSize: 16
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            onClicked: {
                                console.log("[QML]: Executing Startup scenario")
                                dbusClient.executeScenario("scenario-startup")
                            }
                        }

                        Button {
                            text: "🔇 Shutdown"
                            font.pixelSize: 16
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            onClicked: {
                                console.log("[QML]: Executing Shutdown scenario")
                                dbusClient.executeScenario("scenario-shutdown")
                            }
                        }

                        Button {
                            text: "❄️ AC Cool"
                            font.pixelSize: 16
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            onClicked: {
                                console.log("[QML]: AC cooling")
                            }
                        }

                        Button {
                            text: "💡 Lights On"
                            font.pixelSize: 16
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            onClicked: {
                                console.log("[QML]: Lights on")
                                dbusClient.sendDeviceCommand(0x21, "on")
                            }
                        }

                        Button {
                            text: "🌙 Lights Off"
                            font.pixelSize: 16
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            onClicked: {
                                console.log("[QML]: Lights off")
                                dbusClient.sendDeviceCommand(0x21, "off")
                            }
                        }

                        Button {
                            text: "🪟 Curtain"
                            font.pixelSize: 16
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            onClicked: {
                                console.log("[QML]: Toggle curtain")
                            }
                        }
                    }
                }
            }

            // Manual Device Control Section
            GroupBox {
                title: "Device Control"
                Layout.fillWidth: true
                Layout.preferredHeight: 150

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        spacing: 20

                        Button {
                            text: "Projector: ON"
                            Layout.preferredWidth: 150
                            Layout.preferredHeight: 50
                            onClicked: {
                                console.log("[QML]: Projector ON")
                                dbusClient.sendDeviceCommand(0x23, "on")
                            }
                        }

                        Button {
                            text: "Projector: OFF"
                            Layout.preferredWidth: 150
                            Layout.preferredHeight: 50
                            onClicked: {
                                console.log("[QML]: Projector OFF")
                                dbusClient.sendDeviceCommand(0x23, "off")
                            }
                        }

                        Button {
                            text: "Send WoL"
                            Layout.preferredWidth: 150
                            Layout.preferredHeight: 50
                            onClicked: {
                                console.log("[QML]: WoL broadcast")
                                dbusClient.sendWoL("ff:ff:ff:ff:ff:ff")
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // Status Section
            Rectangle {
                id: statusBox
                color: "#e8f4f8"
                border.color: "#1e3a5f"
                border.width: 1
                radius: 5
                Layout.fillWidth: true
                Layout.preferredHeight: 80

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 5

                    Text {
                        text: "Status"
                        font.bold: true
                        font.pixelSize: 16
                    }

                    Text {
                        id: statusText
                        text: "Ready"
                        color: "#333"
                        font.pixelSize: 14
                    }

                    Text {
                        text: "Current Scenario: " + (szCurrentScenario || "None")
                        color: "#666"
                        font.pixelSize: 12
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

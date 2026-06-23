import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: appWindow
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
    property bool bManualLoginPending: false
    property string szManualLoginStatus: ""

    property string warningAlertType: ""
    property string warningMessage: ""
    readonly property string szAppVersion: "v0.1.0"
    property bool bCompactDashboard: appWindow.width <= 1100 || appWindow.height <= 650
    property int iDashboardMargin: bCompactDashboard ? 8 : (appWindow.width < 760 ? 12 : 18)
    property int iDashboardSpacing: bCompactDashboard ? 6 : (appWindow.width < 760 ? 10 : 14)
    property int iHeaderHeight: bCompactDashboard ? 56 : 72
    property int iFooterHeight: bCompactDashboard ? 38 : 48
    property int iScenarioButtonHeight: bCompactDashboard ? 34 : 58
    property int iScenarioPanelBaseHeight: bCompactDashboard ? 38 : 64
    property int iScenarioPanelRowHeight: bCompactDashboard ? 40 : 66
    property int iDeviceCardButtonHeight: bCompactDashboard ? 34 : 48
    property int iDeviceCardBaseHeight: bCompactDashboard ? 56 : 88
    property int iDeviceCardRowHeight: bCompactDashboard ? 40 : 56
    readonly property url compCoreWordmarkLogo: Qt.resolvedUrl("assets/compcore-wordmark-light.png")
    readonly property url compCoreEmblemLogo: Qt.resolvedUrl("assets/compcore-emblem.png")

    function refreshScenarios() {
        var scenarios = dbusClient.getAvailableScenarios()
        scenarioIds = scenarios ? scenarios : []
    }

    function displayScenarioName(scenarioId) {
        return dbusClient.getScenarioDisplayName(scenarioId)
    }

    function scenarioColumnCount() {
        if (dashboard.width < 620)
            return 1
        if (dashboard.width < 900)
            return 2
        return 3
    }

    function deviceColumnCount() {
        if (dashboard.width < 620)
            return 1
        if (dashboard.width < 900)
            return 2
        return 3
    }

    function controlDevice(deviceKey, action) {
        console.log("[QML]: Device control: " + deviceKey + " -> " + action)
        dbusClient.controlClassroomDevice(deviceKey, action)
        dbusClient.setRoomActive()
    }

    function requestManualLogin() {
        if (bManualLoginPending)
            return

        bManualLoginPending = true
        szManualLoginStatus = "Logging in..."

        if (!dbusClient.unlockScreen("")) {
            bManualLoginPending = false
            szManualLoginStatus = "Manual login failed"
        }
    }

    Connections {
        target: dbusClient
        function onAuthStatusChanged(status) {
            console.log("[QML]: Auth status changed: " + status)
            szAuthStatus = status
            if (status === "UNLOCKED") {
                bManualLoginPending = false
                szManualLoginStatus = ""
                var lecturer = dbusClient.getAuthenticatedLecturer()
                if (lecturer.length > 0) {
                    szTeacherName = lecturer
                }
                bIsLocked = false
            } else if (bManualLoginPending && status === "ERROR") {
                bManualLoginPending = false
                szManualLoginStatus = "Manual login failed"
            } else if (status === "LOCKED") {
                bManualLoginPending = false
            }
        }
        function onLecturerAuthenticated(lecturerName, timestamp) {
            console.log("[QML]: Lecturer authenticated: " + lecturerName)
            bManualLoginPending = false
            szManualLoginStatus = ""
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
        function onRoomMonitorAlert(alertType, payloadJson) {
            console.log("[QML]: Room monitor alert: " + alertType)
            var payload = JSON.parse(payloadJson)
            warningAlertType = alertType
            if (alertType === "ROOM_USAGE_OVERRUN") {
                warningMessage = "Warning: Room usage has exceeded 4 hours (" + payload.elapsed_minutes + " minutes)"
            } else if (alertType === "OUT_OF_SCHOOL_HOURS") {
                warningMessage = "Warning: Current time is outside school operating hours"
            }
        }
        function onScenarioStarted(scenarioId) {
            console.log("[QML]: Scenario started: " + scenarioId)
            dbusClient.setRoomActive()
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
        Layout.preferredHeight: appWindow.iDeviceCardButtonHeight
        font.pixelSize: appWindow.bCompactDashboard ? 13 : 15
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
        Layout.preferredHeight: appWindow.iScenarioButtonHeight
        font.pixelSize: appWindow.bCompactDashboard ? 14 : 16
        font.bold: true

        background: Rectangle {
            radius: appWindow.bCompactDashboard ? 6 : 8
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
        Layout.preferredHeight: appWindow.iDeviceCardBaseHeight
                                + Math.ceil(actionRow.children.length / actionColumns)
                                * appWindow.iDeviceCardRowHeight
        radius: 8
        color: "#ffffff"
        border.color: "#d8dee8"
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: appWindow.bCompactDashboard ? 10 : 14
            spacing: appWindow.bCompactDashboard ? 6 : 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    text: title
                    color: "#172033"
                    font.pixelSize: appWindow.bCompactDashboard ? 16 : 17
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: subtitle
                    color: "#64748b"
                    font.pixelSize: appWindow.bCompactDashboard ? 11 : 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            GridLayout {
                id: actionRow
                columns: actionColumns
                Layout.fillWidth: true
                columnSpacing: appWindow.bCompactDashboard ? 6 : 8
                rowSpacing: appWindow.bCompactDashboard ? 6 : 8
            }
        }
    }

    component BrandLogo: Image {
        source: appWindow.compCoreWordmarkLogo
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    component LockBackground: Canvas {
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var bg = ctx.createLinearGradient(0, 0, width, height)
            bg.addColorStop(0.0, "#06111d")
            bg.addColorStop(0.55, "#071d2b")
            bg.addColorStop(1.0, "#030812")
            ctx.fillStyle = bg
            ctx.fillRect(0, 0, width, height)

            var glow = ctx.createRadialGradient(width * 0.78, height * 0.44, 20,
                                                width * 0.78, height * 0.44, width * 0.42)
            glow.addColorStop(0.0, "rgba(26, 218, 216, 0.18)")
            glow.addColorStop(0.5, "rgba(11, 85, 112, 0.12)")
            glow.addColorStop(1.0, "rgba(0, 0, 0, 0)")
            ctx.fillStyle = glow
            ctx.fillRect(0, 0, width, height)

            ctx.save()
            ctx.strokeStyle = "rgba(37, 219, 224, 0.06)"
            ctx.lineWidth = 1
            for (var i = 0; i < 14; ++i) {
                var x = width * 0.56 + i * width * 0.035
                ctx.beginPath()
                ctx.moveTo(x, height * 0.12)
                ctx.lineTo(x + width * 0.12, height * 0.88)
                ctx.stroke()
            }
            ctx.restore()
        }
    }

    component DecorativeBrandVisual: Item {
        id: decorativeVisual
        property real visualSize: 360

        width: visualSize
        height: visualSize

        Canvas {
            anchors.fill: parent
            opacity: 0.9
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                var cx = width / 2
                var cy = height / 2
                var radius = Math.min(width, height) / 2 - 8
                ctx.clearRect(0, 0, width, height)

                ctx.strokeStyle = "rgba(37, 219, 224, 0.14)"
                ctx.lineWidth = 1.4
                for (var i = 0; i < 4; ++i) {
                    ctx.beginPath()
                    ctx.arc(cx, cy, radius * (0.52 + i * 0.14), 0, Math.PI * 2)
                    ctx.stroke()
                }

                ctx.strokeStyle = "rgba(37, 219, 224, 0.7)"
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.arc(cx, cy, radius * 0.86, Math.PI * 0.78, Math.PI * 1.25)
                ctx.stroke()
            }
        }

        Image {
            anchors.centerIn: parent
            width: parent.width * 0.62
            height: parent.height * 0.62
            source: appWindow.compCoreEmblemLogo
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            opacity: 0.46
        }
    }

    component CardReaderIcon: Item {
        id: readerIcon

        Canvas {
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                var s = Math.min(width, height)
                var cardX = width * 0.18
                var cardY = height * 0.34
                var cardW = width * 0.40
                var cardH = height * 0.30
                ctx.clearRect(0, 0, width, height)

                ctx.fillStyle = "#c6e2f1"
                ctx.strokeStyle = "#e8f7ff"
                ctx.lineWidth = Math.max(1.2, s * 0.018)
                var cardRadius = s * 0.035
                ctx.beginPath()
                ctx.moveTo(cardX + cardRadius, cardY)
                ctx.lineTo(cardX + cardW - cardRadius, cardY)
                ctx.quadraticCurveTo(cardX + cardW, cardY, cardX + cardW, cardY + cardRadius)
                ctx.lineTo(cardX + cardW, cardY + cardH - cardRadius)
                ctx.quadraticCurveTo(cardX + cardW, cardY + cardH,
                                     cardX + cardW - cardRadius, cardY + cardH)
                ctx.lineTo(cardX + cardRadius, cardY + cardH)
                ctx.quadraticCurveTo(cardX, cardY + cardH, cardX, cardY + cardH - cardRadius)
                ctx.lineTo(cardX, cardY + cardRadius)
                ctx.quadraticCurveTo(cardX, cardY, cardX + cardRadius, cardY)
                ctx.closePath()
                ctx.fill()
                ctx.stroke()

                ctx.fillStyle = "#061a27"
                ctx.fillRect(cardX + cardW * 0.18, cardY + cardH * 0.22,
                             cardW * 0.42, cardH * 0.24)

                ctx.strokeStyle = "#2ff5ec"
                ctx.lineCap = "round"
                ctx.lineWidth = Math.max(2.4, s * 0.032)
                for (var i = 0; i < 3; ++i) {
                    ctx.beginPath()
                    ctx.arc(cardX + cardW * 1.02, cardY + cardH * 0.5,
                            s * (0.13 + i * 0.12), -0.72, 0.72)
                    ctx.stroke()
                }
            }
        }
    }

    component RfidStatusCard: Rectangle {
        id: rfidCard
        property real scaleFactor: 1.0

        radius: Math.round(16 * scaleFactor)
        color: "#061927"
        border.color: "#13cfd4"
        border.width: Math.max(1, Math.round(1.2 * scaleFactor))

        RowLayout {
            anchors.fill: parent
            anchors.margins: Math.round(24 * rfidCard.scaleFactor)
            spacing: Math.round(24 * rfidCard.scaleFactor)

            Rectangle {
                Layout.preferredWidth: Math.round(132 * rfidCard.scaleFactor)
                Layout.preferredHeight: Math.round(116 * rfidCard.scaleFactor)
                Layout.alignment: Qt.AlignVCenter
                radius: Math.round(14 * rfidCard.scaleFactor)
                color: "#071f30"
                border.color: "#22f0e8"
                border.width: Math.max(1, Math.round(1.4 * rfidCard.scaleFactor))

                CardReaderIcon {
                    anchors.centerIn: parent
                    width: parent.width * 0.74
                    height: parent.height * 0.74
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: Math.round(108 * rfidCard.scaleFactor)
                Layout.alignment: Qt.AlignVCenter
                color: "#2f6b7f"
                opacity: 0.72
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Math.round(10 * rfidCard.scaleFactor)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Math.round(14 * rfidCard.scaleFactor)

                    Image {
                        Layout.preferredWidth: Math.round(46 * rfidCard.scaleFactor)
                        Layout.preferredHeight: Math.round(46 * rfidCard.scaleFactor)
                        source: appWindow.compCoreEmblemLogo
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                    }

                    Text {
                        text: "RFID Scanner Ready"
                        color: "#ffffff"
                        font.pixelSize: Math.round(29 * rfidCard.scaleFactor)
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                Text {
                    text: "Waiting for authorized card..."
                    color: "#25f3ec"
                    font.pixelSize: Math.round(22 * rfidCard.scaleFactor)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: "Place the card near the reader"
                    color: "#dce8ef"
                    opacity: 0.9
                    font.pixelSize: Math.round(18 * rfidCard.scaleFactor)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }

    component SmallStatusIcon: Item {
        id: smallStatusIcon
        property string iconType: "network"
        property color indicatorColor: "#22ff88"
        property real iconSize: 30

        width: iconSize + 10
        height: iconSize + 22

        Canvas {
            id: iconCanvas
            width: smallStatusIcon.iconSize
            height: smallStatusIcon.iconSize
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                var s = Math.min(width, height)
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = "#f2f7fb"
                ctx.lineWidth = Math.max(1.2, s * 0.065)
                ctx.lineCap = "round"
                ctx.lineJoin = "round"

                if (smallStatusIcon.iconType === "shield") {
                    ctx.beginPath()
                    ctx.moveTo(s * 0.50, s * 0.08)
                    ctx.lineTo(s * 0.82, s * 0.20)
                    ctx.lineTo(s * 0.77, s * 0.56)
                    ctx.quadraticCurveTo(s * 0.72, s * 0.76, s * 0.50, s * 0.90)
                    ctx.quadraticCurveTo(s * 0.28, s * 0.76, s * 0.23, s * 0.56)
                    ctx.lineTo(s * 0.18, s * 0.20)
                    ctx.closePath()
                    ctx.stroke()

                    ctx.beginPath()
                    ctx.moveTo(s * 0.36, s * 0.49)
                    ctx.lineTo(s * 0.47, s * 0.60)
                    ctx.lineTo(s * 0.66, s * 0.38)
                    ctx.stroke()
                } else {
                    ctx.beginPath()
                    ctx.arc(s * 0.5, s * 0.5, s * 0.38, 0, Math.PI * 2)
                    ctx.stroke()

                    ctx.beginPath()
                    ctx.moveTo(s * 0.16, s * 0.5)
                    ctx.lineTo(s * 0.84, s * 0.5)
                    ctx.moveTo(s * 0.24, s * 0.32)
                    ctx.lineTo(s * 0.76, s * 0.32)
                    ctx.moveTo(s * 0.24, s * 0.68)
                    ctx.lineTo(s * 0.76, s * 0.68)
                    ctx.stroke()

                    ctx.save()
                    ctx.translate(s * 0.5, s * 0.5)
                    ctx.scale(0.45, 1)
                    ctx.beginPath()
                    ctx.arc(0, 0, s * 0.38, 0, Math.PI * 2)
                    ctx.stroke()
                    ctx.restore()
                }
            }
        }

        Rectangle {
            width: Math.max(8, Math.round(smallStatusIcon.iconSize * 0.30))
            height: width
            radius: width / 2
            color: smallStatusIcon.indicatorColor
            anchors.horizontalCenter: iconCanvas.horizontalCenter
            anchors.top: iconCanvas.bottom
            anchors.topMargin: Math.max(3, Math.round(smallStatusIcon.iconSize * 0.14))
        }
    }

    //
    // LOCK SCREEN
    //
    Rectangle {
        id: lockScreen
        anchors.fill: parent
        visible: bIsLocked
        clip: true

        property real uiScale: Math.max(0.62, Math.min(1.35, Math.min(width / 1280, height / 720)))
        property int edgeMargin: Math.round(72 * uiScale)
        property real leftColumnWidth: Math.min(width * 0.58, 720 * uiScale)

        LockBackground {}

        DecorativeBrandVisual {
            visualSize: Math.min(lockScreen.width * 0.35, lockScreen.height * 0.58)
            anchors.right: parent.right
            anchors.rightMargin: Math.round(lockScreen.edgeMargin * 1.05)
            anchors.verticalCenter: parent.verticalCenter
            visible: lockScreen.width >= 780
        }

        BrandLogo {
            id: loginWordmark
            anchors.left: parent.left
            anchors.leftMargin: lockScreen.edgeMargin
            anchors.top: parent.top
            anchors.topMargin: Math.round(70 * lockScreen.uiScale)
            width: Math.min(lockScreen.leftColumnWidth * 0.48, 270 * lockScreen.uiScale)
            height: width * 0.205
        }

        Column {
            id: loginIntro
            anchors.left: parent.left
            anchors.leftMargin: lockScreen.edgeMargin
            anchors.top: loginWordmark.bottom
            anchors.topMargin: Math.round(78 * lockScreen.uiScale)
            width: lockScreen.leftColumnWidth
            spacing: Math.round(16 * lockScreen.uiScale)

            Text {
                width: parent.width
                text: "SMART PODIUM SYSTEM"
                color: "#ffffff"
                font.pixelSize: Math.round(40 * lockScreen.uiScale)
                font.bold: true
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: "Classroom HMI Login"
                color: "#25f3ec"
                font.pixelSize: Math.round(26 * lockScreen.uiScale)
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: "Scan your lecturer RFID card to unlock classroom controls"
                color: "#dce8ef"
                opacity: 0.92
                font.pixelSize: Math.round(19 * lockScreen.uiScale)
                wrapMode: Text.WordWrap
            }
        }

        RfidStatusCard {
            id: loginRfidCard
            anchors.left: parent.left
            anchors.leftMargin: lockScreen.edgeMargin
            anchors.top: loginIntro.bottom
            anchors.topMargin: Math.round(34 * lockScreen.uiScale)
            width: lockScreen.leftColumnWidth
            height: Math.max(126, Math.round(184 * lockScreen.uiScale))
            scaleFactor: lockScreen.uiScale
        }

        Column {
            id: manualLoginPanel
            anchors.left: parent.left
            anchors.leftMargin: lockScreen.edgeMargin
            anchors.top: loginRfidCard.bottom
            anchors.topMargin: Math.round(18 * lockScreen.uiScale)
            width: Math.min(lockScreen.leftColumnWidth, Math.round(320 * lockScreen.uiScale))
            spacing: Math.round(8 * lockScreen.uiScale)

            Button {
                id: manualLoginButton
                width: parent.width
                height: Math.max(44, Math.round(54 * lockScreen.uiScale))
                enabled: !appWindow.bManualLoginPending
                text: appWindow.bManualLoginPending ? "Logging in..." : "Manual Login"
                font.pixelSize: Math.round(18 * lockScreen.uiScale)
                font.bold: true
                onClicked: appWindow.requestManualLogin()

                background: Rectangle {
                    radius: Math.round(8 * lockScreen.uiScale)
                    color: !manualLoginButton.enabled ? "#6f8798"
                          : manualLoginButton.down ? "#12a8a4"
                          : "#25f3ec"
                    border.color: !manualLoginButton.enabled ? "#8aa0ad" : "#d7fffb"
                    border.width: 1
                }

                contentItem: Text {
                    text: manualLoginButton.text
                    color: manualLoginButton.enabled ? "#061927" : "#dce8ef"
                    font: manualLoginButton.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }

            Text {
                width: parent.width
                visible: appWindow.szManualLoginStatus.length > 0
                text: appWindow.szManualLoginStatus
                color: "#dce8ef"
                opacity: 0.92
                font.pixelSize: Math.round(15 * lockScreen.uiScale)
                elide: Text.ElideRight
            }
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: lockScreen.edgeMargin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Math.round(50 * lockScreen.uiScale)
            spacing: Math.round(22 * lockScreen.uiScale)

            SmallStatusIcon {
                iconType: "network"
                iconSize: Math.round(30 * lockScreen.uiScale)
                indicatorColor: bNetworkConnected ? "#1fff83" : "#ff6b6b"
            }

            SmallStatusIcon {
                iconType: "shield"
                iconSize: Math.round(30 * lockScreen.uiScale)
                indicatorColor: "#2db7ff"
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: lockScreen.edgeMargin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Math.round(52 * lockScreen.uiScale)
            text: appWindow.szAppVersion
            color: "#f2f7fb"
            opacity: 0.92
            font.pixelSize: Math.round(18 * lockScreen.uiScale)
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
            anchors.margins: appWindow.iDashboardMargin
            spacing: appWindow.iDashboardSpacing

            Rectangle {
                id: header
                Layout.fillWidth: true
                Layout.preferredHeight: appWindow.iHeaderHeight
                radius: 8
                color: "#ffffff"
                border.color: "#d8dee8"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: appWindow.bCompactDashboard ? 10 : 14
                    spacing: appWindow.bCompactDashboard ? 8 : 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: "SPS Classroom Control"
                            color: "#172033"
                            font.pixelSize: appWindow.bCompactDashboard ? 21 : (dashboard.width < 760 ? 20 : 24)
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
                        Layout.preferredWidth: appWindow.bCompactDashboard ? 96 : 104
                        Layout.preferredHeight: appWindow.bCompactDashboard ? 38 : 44
                        font.pixelSize: appWindow.bCompactDashboard ? 14 : 15
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

            // Warning banner for room monitoring alerts
            Rectangle {
                id: warningBanner
                Layout.fillWidth: true
                Layout.preferredHeight: warningMessage.length > 0 ? 48 : 0
                radius: 8
                color: "#fef3c7"
                border.color: "#f59e0b"
                border.width: 1
                visible: warningMessage.length > 0

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Text {
                        text: "\u26A0"
                        color: "#d97706"
                        font.pixelSize: 20
                    }

                    Text {
                        text: warningMessage
                        color: "#92400e"
                        font.pixelSize: 14
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    Button {
                        text: "\u2715"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        font.pixelSize: 14
                        flat: true
                        onClicked: {
                            warningMessage = ""
                            warningAlertType = ""
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
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    width: dashboardScroll.availableWidth
                    spacing: appWindow.bCompactDashboard ? 6 : (dashboard.width < 760 ? 12 : 16)

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: scenarioIds.length === 0
                                                ? (appWindow.bCompactDashboard ? 72 : 96)
                                                : appWindow.iScenarioPanelBaseHeight
                                                + Math.ceil(scenarioIds.length / scenarioColumnCount())
                                                * appWindow.iScenarioPanelRowHeight                        
                        radius: 8
                        color: "#ffffff"
                        border.color: "#d8dee8"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: appWindow.bCompactDashboard ? 10 : 14
                            spacing: appWindow.bCompactDashboard ? 8 : 12

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Text {
                                    text: "Scenarios"
                                    color: "#172033"
                                    font.pixelSize: appWindow.bCompactDashboard ? 18 : 20
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
                                columnSpacing: appWindow.bCompactDashboard ? 8 : 10
                                rowSpacing: appWindow.bCompactDashboard ? 8 : 10
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
                        Layout.preferredHeight: appWindow.bCompactDashboard
                                                ? 260
                                                : 82 + Math.ceil(6 / deviceColumnCount()) * 144
                        radius: 8
                        color: "#ffffff"
                        border.color: "#d8dee8"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: appWindow.bCompactDashboard ? 10 : 14
                            spacing: appWindow.bCompactDashboard ? 6 : 12

                            Text {
                                text: "Device Control"
                                color: "#172033"
                                font.pixelSize: appWindow.bCompactDashboard ? 18 : 20
                                font.bold: true
                                Layout.fillWidth: true
                            }

                            GridLayout {
                                columns: deviceColumnCount()
                                Layout.fillWidth: true
                                columnSpacing: appWindow.bCompactDashboard ? 8 : 12
                                rowSpacing: appWindow.bCompactDashboard ? 8 : 12

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
                                    actionColumns: 4

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
                Layout.preferredHeight: appWindow.iFooterHeight
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

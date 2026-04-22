import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    visible: true
    width: 1024
    height: 600
    title: "SPS HMI"
    visibility: Window.FullScreen

    property bool bIsLocked: true
    property string szTeacherName: ""

    Connections {
        target: authBackend
        function onVLoginSuccess(uid, teacherName) {
            console.log("[QML]: Catch log in signal from D-Bus of " + teacherName)
            szTeacherName = teacherName
            bIsLocked = false
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

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 20

            Text {
                text: "PODIUM CONTROL DEVICE"
                color: "white"
                font.pixelSize: 32
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: "Swipe RFID to activate smart podium"
                color: "#00bfff"
                font.pixelSize: 24
                Layout.alignment: Qt.AlignHCenter
            }

            Button {
                text: "SUCCESS"
                Layout.alignment: Qt.AlignHCenter
                onClicked: {
                    authBackend.vSimulateRfidScan()
                }
            }
        }
    }

    //
    // DASHBOARD
    //
    Rectangle {
        id: dashboard
        anchors.fill: parent
        color: "#f4f4f9"
        visible: !bIsLocked

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 30

            Text {
                text: "HELLO, " + szTeacherName.toUpperCase()
                color: "#333"
                font.pixelSize: 36
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            RowLayout {
                spacing: 20
                Layout.alignment: Qt.AlignHCenter

                Button {
                    text: "TURN ON PROJECTOR"
                    font.pixelSize: 20
                    width: 200; height: 100
                }

                Button {
                    text: "ON CURTAIN"
                    font.pixelSize: 20
                    width: 200; height: 100
                }
            }

            Button {
                text: "LOG OUT"
                Layout.alignment: Qt.AlignHCenter
                onClicked: {
                    isLocked = true // Khóa lại màn hình
                    currentTeacher = ""
                }
            }
        }
    }
}

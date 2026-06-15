set(TARGET_NAME appHmi)

qt_add_dbus_interface(DBUS_PROXY_SRCS ${DBUS_INTERFACES_DIR}/com.sps.auth.xml auth_interface)
qt_add_dbus_interface(DBUS_PROXY_SRCS ${DBUS_INTERFACES_DIR}/com.sps.router.xml router_interface)
qt_add_dbus_interface(DBUS_PROXY_SRCS ${DBUS_INTERFACES_DIR}/com.sps.engine.xml engine_interface)
qt_add_dbus_interface(DBUS_PROXY_SRCS ${DBUS_INTERFACES_DIR}/com.sps.netmgr.xml netmgr_interface)


set(SOURCES
	main.cpp
	app_dbus_client.cpp
	${DBUS_PROXY_SRCS}
)

set(HEADERS
	app_dbus_client.h
)

qt_add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

qt_add_qml_module(${TARGET_NAME}
	URI "SpsHmi"
	VERSION 1.0.0
	QML_FILES
		qml/Main.qml
)

target_link_libraries(${TARGET_NAME} PRIVATE
        SpsCommon
        Qt6::Core
	Qt6::Gui
	Qt6::Qml
	Qt6::Quick
        Qt6::DBus
)

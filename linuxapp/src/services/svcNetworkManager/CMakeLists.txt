set(TARGET_NAME svcNetworkManager)

# Generate C++ Adaptor from file XML
qt_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
        ${DBUS_INTERFACES_DIR}/com.sps.netmgr.xml
        network_manager.h
	NetworkManager
)

set (SOURCES
        main.cpp
	network_manager.cpp
	mqtt_client.cpp
        ${DBUS_ADAPTOR_SRCS}
)

set(HEADERS
	network_manager.h
	mqtt_client.h
	mqtt_defines.h
)

add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${TARGET_NAME} PRIVATE
        SpsCommon
        Qt6::Core
        Qt6::DBus
	Qt6::Network
)

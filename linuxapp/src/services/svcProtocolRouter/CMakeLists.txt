set(TARGET_NAME svcProtocolRouter)

# Generate C++ Adaptor from file XML
qt_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
        ${DBUS_INTERFACES_DIR}/com.sps.router.xml
        protocol_router.h
        ProtocolRouter
)

set (SOURCES
        main.cpp
        protocol_router.cpp
        mcu_engine.cpp
        uart_port.cpp
	uart_frame.cpp
        ${DBUS_ADAPTOR_SRCS}
)

set(HEADERS
        protocol_router.h
        mcu_engine.h
	uart_port.h
        uart_frame.h
	uart_defines.h
)

add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${TARGET_NAME} PRIVATE
        SpsCommon
        Qt6::Core
        Qt6::DBus
        Qt6::SerialPort
)

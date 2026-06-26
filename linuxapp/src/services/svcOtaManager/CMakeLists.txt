set(TARGET_NAME svcOtaManager)

# Generate C++ Adaptor from file XML
qt_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
        ${DBUS_INTERFACES_DIR}/com.sps.otamanager.xml
        ota_manager.h
        OtaManager
)

set (SOURCES
        main.cpp
        ota_manager.cpp
        ${DBUS_ADAPTOR_SRCS}
)

set(HEADERS
        ota_manager.h
)

add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${TARGET_NAME} PRIVATE
        SpsCommon
        Qt6::Core
        Qt6::DBus
        Qt6::Network
)

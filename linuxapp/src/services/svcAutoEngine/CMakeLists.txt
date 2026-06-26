set(TARGET_NAME svcAutoEngine)

# Generate C++ Adaptor from file XML
qt_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
        ${DBUS_INTERFACES_DIR}/com.sps.engine.xml
        scenario_engine.h
        ScenarioEngine
)

set (SOURCES
        main.cpp
        scenario_engine.cpp
        device_map.cpp
        router_command_normalizer.cpp
        ${DBUS_ADAPTOR_SRCS}
)

set(HEADERS
	scenario_engine.h
	device_map.h
        router_command_normalizer.h
)

add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${TARGET_NAME} PRIVATE
        SpsCommon
        Qt6::Core
        Qt6::DBus
)

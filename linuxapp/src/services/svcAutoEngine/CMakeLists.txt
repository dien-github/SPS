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
        ${DBUS_ADAPTOR_SRCS}
)

set(HEADERS
	scenario_engine.h
)

add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${TARGET_NAME} PRIVATE
        SpsCommon
        Qt6::Core
        Qt6::DBus
)

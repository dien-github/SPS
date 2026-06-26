set(TARGET_NAME svcAuthentication)

# Generate C++ Adaptor from file XML
qt_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
	${DBUS_INTERFACES_DIR}/com.sps.auth.xml
	auth_service.h
	AuthService
)

set (SOURCES
	main.cpp
	auth_service.cpp
	rfid_reader.cpp
	${DBUS_ADAPTOR_SRCS}
)

set(HEADERS
	auth_service.h
	rfid_reader.h
)


add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${TARGET_NAME} PRIVATE
	SpsCommon
	Qt6::Core
	Qt6::DBus
	Qt6::SerialPort
)

set(DBUS_INTERFACES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/interfaces)

add_subdirectory(common)

add_subdirectory(services/svcAuthentication)
add_subdirectory(services/svcAutoEngine)
add_subdirectory(services/svcNetworkManager)
add_subdirectory(services/svcProtocolRouter)

add_subdirectory(apps/appHmi)

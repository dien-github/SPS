set(COMMON_SOURCES
	sps_service_base.cpp
)

set(COMMON_HEADERS
	sps_logger.h
	sps_constants.h
	sps_device_models.h
	sps_service_base.h
	sps_message_queue.h
)

add_library(SpsCommon STATIC ${COMMON_SOURCES} ${COMMON_HEADERS})

target_include_directories(SpsCommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(SpsCommon PUBLIC Qt6::Core Qt6::DBus)

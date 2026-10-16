cmake_minimum_required(VERSION 3.20)

# Generated C++ for the headless client wire protocol (headless.proto).
add_library(headless_proto STATIC headless.proto)

protobuf_generate(
	TARGET headless_proto
	LANGUAGE cpp
	IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
	PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(headless_proto PUBLIC protobuf::libprotobuf)
target_include_directories(headless_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

set_property(TARGET headless_proto PROPERTY FOLDER libraries)

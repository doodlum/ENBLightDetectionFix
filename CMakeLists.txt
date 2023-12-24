cmake_minimum_required(VERSION 3.20)

project(
	ENBLightAffectsStealth
	VERSION 1.0.0
	LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(XSEPlugin)

find_package(EASTL CONFIG REQUIRED)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
	EASTL
)
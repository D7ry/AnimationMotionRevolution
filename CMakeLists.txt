cmake_minimum_required(VERSION 3.21)

# CommonLibSSE-NG and the selected vcpkg triplet use the dynamic MSVC runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "")

set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "Enable Xbyak trampoline support" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY ON CACHE BOOL "Validate trampoline patch boundaries" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Build for Skyrim SE" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Build for Skyrim AE" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Build for Skyrim VR" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Build CommonLibSSE tests" FORCE)
option(AMR_ENABLE_TRUEHUD_DEBUG "Build TrueHUD debug visualizations" ON)

project(
	AnimationMotionRevolution
	VERSION 2.0.0
	LANGUAGES CXX
)

set(COMMONLIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG")
if(NOT EXISTS "${COMMONLIB_PATH}/include/RE/Skyrim.h")
	message(FATAL_ERROR "CommonLibSSE-NG is missing. Run: git submodule update --init --recursive")
endif()

find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)
find_path(XBYAK_INCLUDE_DIRS "xbyak/xbyak.h" REQUIRED)

add_subdirectory("${COMMONLIB_PATH}" CommonLibSSE EXCLUDE_FROM_ALL)
include("${COMMONLIB_PATH}/cmake/CommonLibSSE.cmake")

set(headers
	include/AnimMotionHandler.h
	include/Hooks.h
	include/Motion.h
	include/pch.h
	include/Settings.h
	include/utils/Logger.h
	include/utils/Trampoline.h
)

set(sources
	source/Hooks.cpp
	source/main.cpp
	source/pch.cpp
	source/Settings.cpp
)

if(AMR_ENABLE_TRUEHUD_DEBUG)
	list(APPEND headers
		include/TrueHUDAPI.h
		include/TrueHUDIntegration.h
	)
	list(APPEND sources source/TrueHUDIntegration.cpp)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${headers} ${sources})

add_commonlibsse_plugin(
	${PROJECT_NAME}
	NAME "AnimationMotionRevolution"
	AUTHOR "alexsylex"
	USE_ADDRESS_LIBRARY
	SOURCES ${headers} ${sources}
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(
	${PROJECT_NAME}
	PRIVATE
		_UNICODE
		UNICODE
		NOMINMAX
		$<$<BOOL:${AMR_ENABLE_TRUEHUD_DEBUG}>:AMR_ENABLE_TRUEHUD_DEBUG>
)
target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/include"
		"${SIMPLEINI_INCLUDE_DIRS}"
		"${XBYAK_INCLUDE_DIRS}"
)

target_precompile_headers(${PROJECT_NAME} PRIVATE include/pch.h)

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /EHsc /MP /W4 /utf-8 /permissive- /Zc:preprocessor)
	target_link_options(
		${PROJECT_NAME}
		PRIVATE
			"$<$<CONFIG:Debug>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:Release>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF>"
	)
endif()

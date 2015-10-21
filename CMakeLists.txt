cmake_minimum_required(VERSION 3.1.0)
project(osvrUnityRenderingPlugin)
set(CMAKE_BUILD_TYPE Release)

#-----------------------------------------------------------------------------
# Local CMake Modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")


#-----------------------------------------------------------------------------
# This looks for an osvrConfig.cmake file - most of the time it can be
# autodetected but you might need to specify osvr_DIR to be something like
# C:/Users/Ryan/Desktop/build/OSVR-Core-vc12 or
# C:/Users/Ryan/Downloads/OSVR-Core-Snapshot-v0.1-406-gaa55515-build54-vs12-32bit
# in the CMake GUI or command line
find_package(osvr REQUIRED)

# Add one of these libraries for each vendor that we've got a driver
# for and add a No driver that tells that it is unavailable when we
# don't find the driver library.
find_package(OpenGL)
find_package(GLEW)
find_package(SDL2)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Unity")

# RenderManager
find_path(RENDERMANAGER_INCLUDE_DIR RenderManager.h)
find_library(RENDERMANAGER_LIBRARY osvrRenderManager)

if (RENDERMANAGER_INCLUDE_DIR AND RENDERMANAGER_LIBRARY)
    set(RENDERMANAGER_FOUND TRUE)
    include_directories(${RENDERMANAGER_INCLUDE_DIR})
endif ()

# RenderManagerExport
find_path(RENDERMANAGEREXPORT_INCLUDE_DIR osvrRenderManager_Export.h)
if (RENDERMANAGEREXPORT_INCLUDE_DIR)
    set(RENDERMANAGEREXPORT_FOUND TRUE)
    include_directories(${RENDERMANAGEREXPORT_INCLUDE_DIR})
endif ()

set (osvrUnityRenderingPlugin_SOURCES
	OsvrRenderingPlugin.cpp
)
add_library(osvrUnityRenderingPlugin SHARED OsvrRenderingPlugin.cpp)
if (OPENGL_FOUND AND GLEW_FOUND AND SDL2_FOUND AND RM_FOUND)
	target_include_directories(osvrUnityRenderingPlugin PRIVATE ${OPENGL_INCLUDE_DIRS})
	target_link_libraries(osvrUnityRenderingPlugin PRIVATE ${OPENGL_LIBRARY} ${RENDERMANAGER_LIBRARY} GLEW::GLEW SDL2::SDL2)
	# Handle static glew.
	if(GLEW_LIBRARY MATCHES ".*s.lib")
		target_compile_definitions(osvrUnityRenderingPlugin PRIVATE GLEW_STATIC)
	endif()
endif()



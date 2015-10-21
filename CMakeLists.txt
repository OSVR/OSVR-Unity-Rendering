cmake_minimum_required(VERSION 3.1.0)
project(osvrUnityRenderingPlugin)

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
find_package(Boost 1.58.0) 
find_package(osvrRenderManager REQUIRED)

include_directories(${osvrRenderManager_INCLUDE_DIR})

include_directories("${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Unity")

set (osvrUnityRenderingPlugin_SOURCES
	OsvrRenderingPlugin.cpp
)
add_library(osvrUnityRenderingPlugin SHARED OsvrRenderingPlugin.cpp)
set_target_properties(osvrUnityRenderingPlugin PROPERTIES LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/OsvrRenderingPlugin.def")
target_link_libraries(osvrUnityRenderingPlugin osvr::osvrClientKit)
target_link_libraries(osvrUnityRenderingPlugin osvrRenderManager)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS}) 
    target_link_libraries(osvrUnityRenderingPlugin ${Boost_LIBRARIES})
endif()
if (OPENGL_FOUND AND GLEW_FOUND AND SDL2_FOUND)
	target_include_directories(osvrUnityRenderingPlugin PRIVATE ${OPENGL_INCLUDE_DIRS})
	target_link_libraries(osvrUnityRenderingPlugin ${OPENGL_LIBRARY} GLEW::GLEW SDL2::SDL2)
	# Handle static glew.
	if(GLEW_LIBRARY MATCHES ".*s.lib")
		target_compile_definitions(osvrUnityRenderingPlugin PRIVATE GLEW_STATIC)
	endif()
endif()



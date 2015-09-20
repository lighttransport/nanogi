#
#  nanogi - A small reference GI renderer
# 
#  Copyright (c) 2015 Light Transport Entertainment Inc.
#  All rights reserved.
# 
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#  * Redistributions of source code must retain the above copyright
#  notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#  notice, this list of conditions and the following disclaimer in the
#  documentation and/or other materials provided with the distribution.
#  * Neither the name of the <organization> nor the
#  names of its contributors may be used to endorse or promote products
#  derived from this software without specific prior written permission.
# 
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
#  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
#  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#  DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
#  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
#  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
#  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

cmake_minimum_required(VERSION 3.0)

include(CMakeParseArguments)

project(nanogi)

# --------------------------------------------------------------------------------

##
## Module path, prefix, architecture, build type
##

# Add module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if (MSVC)
	# Set prefix path according to generator
	# Currently only VC12 is supported
	if (CMAKE_GENERATOR STREQUAL "Visual Studio 12 2013 Win64")
		set(GENERATOR_PREFIX "vc12")
	else()
		message(FATAL_ERROR "Invalid generator")
	endif()
	list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external")
	list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/${GENERATOR_PREFIX}")
endif()

# Set architecture
if (CMAKE_CL_64)
	set(CMAKE_LIBRARY_ARCHITECTURE "x64")
else()
	set(CMAKE_LIBRARY_ARCHITECTURE "i386")
endif()

# Set Release as the default build mode
if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING
		"Choose build type (Debug, Release, RelWithDebInfo, or MinSizeRel)" FORCED)
endif()

# --------------------------------------------------------------------------------

##
## External libraries
##

# Boost
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
add_definitions(-DBOOST_ALL_NO_LIB)
find_package(Boost 1.54 REQUIRED COMPONENTS program_options filesystem system regex coroutine context)
include_directories(${Boost_INCLUDE_DIRS})

# GLM
find_package(GLM REQUIRED)
include_directories(${GLM_INCLUDE_DIRS})

if (MSVC)
	# Qt
	list(APPEND CMAKE_PREFIX_PATH $ENV{QTDIR})
	list(APPEND CMAKE_LIBRARY_PATH "C:\\Program Files (x86)\\Windows Kits\\8.0\\Lib\\win8\\um\\x64")
	set(CMAKE_INCLUDE_CURRENT_DIR ON)
	find_package(Qt5Widgets REQUIRED)
	find_package(Qt5UiTools REQUIRED)
	find_package(Qt5Gui REQUIRED)
	set(QT_BINARY_FILES_RELEASE "icudt53" "icuin53" "icuuc53" "Qt5Core" "Qt5Gui" "Qt5Widgets")
	set(QT_BINARY_FILES_DEBUG   "icudt53" "icuin53" "icuuc53" "Qt5Cored" "Qt5Guid" "Qt5Widgetsd")

	# GLEW
	set(GLEW_USE_STATIC_LIBS ON)
	find_package(GLEW)
	if (GLEW_FOUND)
		include_directories(${GLEW_INCLUDE_DIRS})
		add_definitions(${GLEW_DEFINITIONS})
	endif()
endif()

# Assimp
find_package(Assimp REQUIRED)
include_directories(${ASSIMP_INCLUDE_DIRS})

# FreeImage
find_package(FreeImage REQUIRED)
include_directories(${FREEIMAGE_INCLUDE_DIRS})

# Embree
find_package(Embree REQUIRED)
include_directories(${EMBREE_INCLUDE_DIRS})

# yaml-cpp
find_package(YamlCpp REQUIRED)
include_directories(${YAMLCPP_INCLUDE_DIRS})

# ctemplate
find_package(CTemplate REQUIRED)
add_definitions(-DCTEMPLATE_DLL_DECL=)           # Use static library
include_directories(${CTEMPLATE_INCLUDE_DIRS})

# TBB
find_package(TBB REQUIRED)
include_directories(${TBB_INCLUDE_DIRS})

# Eigen3
find_package(Eigen3 REQUIRED)
include_directories(${EIGEN3_INCLUDE_DIRS})

# Add the binary directory to the include directories for generated headers
include_directories(${PROJECT_BINARY_DIR})

# --------------------------------------------------------------------------------

##
## Compiler options
##

if (MSVC)
	# Ignore linker warnings in VS
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /ignore:4099")
	add_definitions(-D_SCL_SECURE_NO_WARNINGS)

	# Increase stack size to 10MB
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:10000000")

	# Warning level 4, treat warning as errors
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")

	# Add floating-point flag
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:strict")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHa")

    # Platform flag (used in boost)
    # cf. http://stackoverflow.com/questions/9742003/platform-detection-in-cmake
    set(_VERSION ${CMAKE_SYSTEM_VERSION})
    string(REPLACE "." "" _VERSION ${_VERSION})
    string(REGEX REPLACE "([0-9])" "0\\1" _VERSION ${_VERSION})
    add_definitions(-D_WIN32_WINNT=0x${_VERSION})
endif()

if (CMAKE_COMPILER_IS_GNUCXX)
	# Enabling C++11 for gcc
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

	# If the build type is Debug, define macro
	if (CMAKE_BUILD_TYPE STREQUAL Debug)
		add_definitions(-D_DEBUG)
	endif()

	# Enable all warnings, treat warning as errors
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")

	# Ignore unknown pragma
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES Clang)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -stdlib=libc++")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas")
endif()

# --------------------------------------------------------------------------------

##
## Output directories
##

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# --------------------------------------------------------------------------------

##
## Include directories
##

# Include directory for libraries
include_directories("include")

# Specify to use solution directories
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# --------------------------------------------------------------------------------

##
## Add projects
##

function(add_project)
	# Parse arguments
	cmake_parse_arguments(_ARG "" "NAME" "SOURCE_FILES;LIBRARY_FILES" ${ARGN})

	# Add project
	add_executable(${_ARG_NAME} ${_ARG_SOURCE_FILES})
	target_link_libraries(${_ARG_NAME} ${Boost_LIBRARIES} ${_ARG_LIBRARY_FILES})

	# Solution directory
	set_target_properties(${_ARG_NAME} PROPERTIES FOLDER "apps")
endfunction()

function(add_qt_project)
	# Parse arguments
	cmake_parse_arguments(_ARG "" "NAME" "SOURCE_FILES;LIBRARY_FILES;UI_FILES;RESOURCE_FILES" ${ARGN})

	# Qt's UI & resource files
	qt5_wrap_cpp(_SOURCE_FILES ${_ARG_SOURCE_FILES})
	qt5_wrap_ui(_UI_HEADER_FILES ${_ARG_UI_FILES})
	qt5_add_resources(_RESOURCE_FILES ${_ARG_RESOURCE_FILES})

	# Add project
	set_source_files_properties(${_SOURCE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
	add_executable(${_ARG_NAME} ${_ARG_SOURCE_FILES} ${_SOURCE_FILES} ${_UI_HEADER_FILES} ${_RESOURCE_FILES})
	target_link_libraries(${_ARG_NAME} Qt5::Widgets Qt5::UiTools Qt5::Gui ${Boost_LIBRARIES} ${_ARG_LIBRARY_FILES})

	# Solution directory
	set_target_properties(${_ARG_NAME} PROPERTIES FOLDER "apps")
endfunction()

list(APPEND _RENDERER_LIBRARY_FILES ${ASSIMP_LIBRARIES} ${FREEIMAGE_LIBRARIES} ${EMBREE_LIBRARIES} ${YAMLCPP_LIBRARIES} ${TBB_LIBRARIES})
set(_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/nanogi")

add_project(
	NAME "nanogi"
	SOURCE_FILES
		"src/nanogi.cpp"
		"src/tinyexr.cc"
		"${_INCLUDE_DIR}/macros.hpp"
		"${_INCLUDE_DIR}/basic.hpp"
		"${_INCLUDE_DIR}/rt.hpp"
		"${_INCLUDE_DIR}/bdpt.hpp"
	LIBRARY_FILES ${_RENDERER_LIBRARY_FILES} ${CTEMPLATE_LIBRARIES})

if (MSVC)
	add_qt_project(
		NAME "nanogi-viewer"
		SOURCE_FILES
			"src/nanogi-viewer.cpp"
			"${_INCLUDE_DIR}/macros.hpp"
			"${_INCLUDE_DIR}/basic.hpp"
			"${_INCLUDE_DIR}/rt.hpp"
			"${_INCLUDE_DIR}/gl.hpp"
		UI_FILES "src/nanogi-viewer.ui"
		LIBRARY_FILES ${_RENDERER_LIBRARY_FILES} ${GLEW_LIBRARIES})
endif()

# --------------------------------------------------------------------------------

##
## Copy dynamic libraries
##

if(WIN32)
	# Create empty target
	add_custom_target(copydlls)

	# Add a custom command to copy a dynamic library to the same directory as executables
	function(add_custom_command_copy_dll BINARY_FILE_NAME)
		add_custom_command(
			TARGET copydlls
			PRE_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					${BINARY_FILE_NAME}
					$<TARGET_FILE_DIR:nanogi>)
	endfunction()

	function(add_custom_command_copy_dll_release_debug)
		cmake_parse_arguments(_ARG "" "RELEASE;DEBUG" "" ${ARGN})
		add_custom_command(
			TARGET copydlls
			PRE_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"$<$<CONFIG:release>:${_ARG_RELEASE}>$<$<CONFIG:debug>:${_ARG_DEBUG}>"
					"$<TARGET_FILE_DIR:nanogi>")
	endfunction()

	# Libraries in external directory
	file(GLOB EXT_BINARY_FILES "${CMAKE_SOURCE_DIR}/external/${GENERATOR_PREFIX}/bin/${CMAKE_LIBRARY_ARCHITECTURE}/*.dll")
	foreach(BINARY_FILE_NAME ${EXT_BINARY_FILES})
		add_custom_command_copy_dll(${BINARY_FILE_NAME})
	endforeach()

	# Qt
	list(LENGTH QT_BINARY_FILES_RELEASE count)
	math(EXPR count "${count}-1")
	foreach(i RANGE ${count})
		list(GET QT_BINARY_FILES_RELEASE ${i} BINARY_FILE_NAME_RELEASE)
		list(GET QT_BINARY_FILES_DEBUG   ${i} BINARY_FILE_NAME_DEBUG)
		set(BINARY_FILE_PATH_RELEASE "$ENV{QTDIR}/bin/${BINARY_FILE_NAME_RELEASE}.dll")
		set(BINARY_FILE_PATH_DEBUG   "$ENV{QTDIR}/bin/${BINARY_FILE_NAME_DEBUG}.dll")
		add_custom_command_copy_dll_release_debug(RELEASE ${BINARY_FILE_PATH_RELEASE} DEBUG ${BINARY_FILE_PATH_DEBUG})
	endforeach()
	add_custom_command(
		TARGET copydlls
		PRE_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"$ENV{QTDIR}/plugins/platforms"
				"$<TARGET_FILE_DIR:nanogi>/platforms")

	# TBB
	add_custom_command_copy_dll_release_debug(
		RELEASE "$ENV{TBB_ROOT}/bin/$ENV{TBB_ARCH_PLATFORM}/${GENERATOR_PREFIX}/tbb.dll"
		DEBUG "$ENV{TBB_ROOT}/bin/$ENV{TBB_ARCH_PLATFORM}/${GENERATOR_PREFIX}/tbb_debug.dll")

endif()

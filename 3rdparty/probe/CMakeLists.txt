cmake_minimum_required(VERSION 3.16)

project(probe VERSION 0.5.2 LANGUAGES C CXX)

# options
option(PROBE_BUILD_WITH_QT  "Build probe with Qt."      ON)

# compiler options
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Default build type: Release." FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Zc:preprocessor /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-trigraphs)
endif()

if(PROBE_BUILD_WITH_QT)
    if (NOT QT_DEFAULT_MAJOR_VERSION)
        set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "Qt major version to use (5 or 6), default: 5.")
    endif ()

    find_package(Qt${QT_DEFAULT_MAJOR_VERSION} COMPONENTS Core REQUIRED)
endif()

# dependencies
if(UNIX AND NOT APPLE)
    find_package(X11)

    if(X11_FOUND)
        if(NOT X11_Xrandr_FOUND)
            message(FATAL_ERROR "Xrandr is required, please install libxrandr-dev")
        endif()
        add_compile_definitions(PROBE_WITH_X11)
    else()
        message(STATUS "Can not find X11, build probe without X11")
    endif()
endif()

# version header file
configure_file(${PROJECT_SOURCE_DIR}/version.h.in ${PROJECT_BINARY_DIR}/version.h @ONLY)

# source files
file(GLOB_RECURSE PROBE_HEADERS include/probe/*.h)
file(GLOB_RECURSE PROBE_SOURCES src/*.cpp src/*.mm)

add_library(${PROJECT_NAME} ${PROBE_SOURCES} ${PROBE_HEADERS})
add_library(probe::probe ALIAS probe)

target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        $<$<PLATFORM_ID:Windows>:dwmapi>
        $<$<PLATFORM_ID:Windows>:strmiids>
        $<$<PLATFORM_ID:Windows>:ntdll>
        $<$<PLATFORM_ID:Windows>:Shcore>
        $<$<PLATFORM_ID:Windows>:DXGI>
        $<$<PLATFORM_ID:Windows>:D3D11>
        $<$<PLATFORM_ID:Windows>:SetupAPI>
        $<$<PLATFORM_ID:Windows>:Iphlpapi>
        $<$<PLATFORM_ID:Windows>:Ws2_32>
        $<$<PLATFORM_ID:Windows>:Shlwapi>
        $<$<PLATFORM_ID:Windows>:runtimeobject>
        $<$<PLATFORM_ID:Linux>:pthread>
        $<$<AND:$<PLATFORM_ID:Linux>,$<BOOL:${X11_Xrandr_FOUND}>>:X11::Xrandr>
        $<$<PLATFORM_ID:Linux>:dl>
        $<$<BOOL:${PROBE_BUILD_WITH_QT}>:Qt${QT_DEFAULT_MAJOR_VERSION}::Core>
)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        VERSION ${PROJECT_VERSION}

        PUBLIC_HEADER "${PROBE_HEADERS}"

        RUNTIME_OUTPUT_DIRECTORY "bin/$<0:>"
        LIBRARY_OUTPUT_DIRECTORY "lib/$<0:>"
        ARCHIVE_OUTPUT_DIRECTORY "lib/$<0:>"

        DEBUG_POSTFIX "d"
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC PROBE_SHARED PRIVATE PROBE_BUILDING)
endif()

if(PROBE_BUILD_WITH_QT)
    target_compile_definitions(${PROJECT_NAME} PUBLIC BUILD_WITH_QT)
endif()

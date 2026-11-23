cmake_minimum_required(VERSION 3.14)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# NMake/JOM can lose dependencies when localized MSVC /showIncludes output is
# not parsed correctly. Scan includes with CMake so header edits rebuild all
# affected objects instead of mixing old inline and new out-of-line definitions.
if(CMAKE_GENERATOR MATCHES "^NMake Makefiles")
    set(CMAKE_DEPENDS_USE_COMPILER FALSE)
endif()

project(ScreenCapturer VERSION 1.5.1 LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/app)

if(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/app/lib/debug)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/app/lib/debug)
else()
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/app/lib/release)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/app/lib/release)
endif()

# Win32 unicode fix and msvc utf8 flag
if(WIN32)
    add_definitions(-DUNICODE)
    add_definitions(-D_UNICODE)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_NOMINMAX)
    add_definitions(-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR) # VS 17.10: https://github.com/microsoft/STL/issues/4730
    add_definitions(-DDISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)

    # For ng-log
    add_definitions(-DNGLOG_NO_ABBREVIATED_SEVERITIES)


    if(MSVC)
        add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
        add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
        # add_compile_options("$<$<C_COMPILER_ID:MSVC>:/await>")
        # add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/await>")
    endif()
endif()

# For Probe lib
add_definitions(-DBUILD_WITH_QT)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()


# list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/3rdparty/cmake-modules")
set(JSON_BuildTests             OFF CACHE BOOL "Build nlohmann/json tests")

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Gui OpenGL Widgets OpenGLWidgets LinguistTools REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui OpenGL Widgets OpenGLWidgets LinguistTools REQUIRED)

if(WIN32)
    find_package(Qt6 REQUIRED COMPONENTS Network)
elseif(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(SCREENCAPTURER_SPARKLE_FRAMEWORK "" CACHE PATH "Path to Sparkle.framework")
    set(SCREENCAPTURER_SPARKLE_PUBLIC_KEY "" CACHE STRING "Sparkle EdDSA public key")
    set(SCREENCAPTURER_MAC_SIGN_IDENTITY "-" CACHE STRING "Identity used to sign the macOS distribution bundle")
    if(SCREENCAPTURER_SPARKLE_FRAMEWORK OR SCREENCAPTURER_SPARKLE_PUBLIC_KEY)
        if(NOT EXISTS "${SCREENCAPTURER_SPARKLE_FRAMEWORK}/Sparkle" OR
           NOT SCREENCAPTURER_SPARKLE_PUBLIC_KEY MATCHES "^[A-Za-z0-9+/]+=$")
            message(FATAL_ERROR "Set both the Sparkle.framework path and its EdDSA public key")
        endif()
        set(SCREENCAPTURER_WITH_SPARKLE ON)
    endif()
endif()

# if (WIN32)
#     # GUI/console
#     set(CMAKE_WIN32_EXECUTABLE $<IF:$<CONFIG:DEBUG>,OFF,ON>)

#     find_package(FFmpeg 6 REQUIRED)
# elseif (UNIX AND NOT APPLE)
#     find_package(FFmpeg 4.2 REQUIRED)

#     find_package(Qt6 COMPONENTS X11Extras REQUIRED)
# endif ()

# 3rdparty
set(3RDPARTYPATH ${CMAKE_SOURCE_DIR}/3rdparty)
include(${3RDPARTYPATH}/3rdparty.cmake)

set(Resources "${PROJECT_SOURCE_DIR}/resources")
set(ICONS "${Resources}/res.qrc")
set(WINICON "${Resources}/mainicon.rc")

# src
add_subdirectory(3rdparty/json EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/fmt  EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/probe EXCLUDE_FROM_ALL)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
add_subdirectory(3rdparty/ng-log EXCLUDE_FROM_ALL)
add_subdirectory(src)

if (APPLE)
find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${Qt6_DIR}/../../../bin")
if(NOT MACDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "macdeployqt was not found. Set MACDEPLOYQT_EXECUTABLE to the Qt tool path.")
endif()

set(mac_sign_options)
if(NOT SCREENCAPTURER_MAC_SIGN_IDENTITY STREQUAL "-")
    list(APPEND mac_sign_options --options runtime --timestamp)
endif()

add_custom_target(
    buildOSXApp ALL
    DEPENDS ScreenCapturer
)

add_custom_command(TARGET buildOSXApp POST_BUILD

        # Copy translations file
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/app/ScreenCapturer.app/Contents/MacOS/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_BINARY_DIR}/src/Main/Capturer_en_US.qm"
            "${CMAKE_SOURCE_DIR}/app/ScreenCapturer.app/Contents/MacOS/translations/Capturer_en_US.qm"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_BINARY_DIR}/src/Main/Capturer_zh_CN.qm"
                "${CMAKE_SOURCE_DIR}/app/ScreenCapturer.app/Contents/MacOS/translations/Capturer_zh_CN.qm"

        # Make output folder
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/app/Final"
        COMMAND /usr/bin/ditto
            "${CMAKE_SOURCE_DIR}/app/ScreenCapturer.app"
            "${CMAKE_SOURCE_DIR}/app/Final/ScreenCapturer.app"

        # For macOS, you might want to use macdeployqt instead
        COMMAND ${MACDEPLOYQT_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/app/Final/ScreenCapturer.app"

        # codesign app
        COMMAND /usr/bin/codesign
                --deep --force ${mac_sign_options}
                --sign "${SCREENCAPTURER_MAC_SIGN_IDENTITY}" "${CMAKE_SOURCE_DIR}/app/Final/ScreenCapturer.app"
        VERBATIM
)
endif()


option(SCREENCAPTURER_BUILD_SETTINGS_TESTS "Build isolated settings migration tests" OFF)
if(SCREENCAPTURER_BUILD_SETTINGS_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

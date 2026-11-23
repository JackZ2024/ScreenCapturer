cmake_minimum_required(VERSION 3.14)

set(TARGET_NAME ${PROJECT_NAME})

FILE(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
FILE(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "*.h" "*.hpp")

configure_file(./version.h.in ${PROJECT_BINARY_DIR}/version.h @ONLY)

# set(TS_FILES ${Resources}/languages/Capturer_en_US.ts ${Resources}/languages/Capturer_zh_CN.ts)
# qt6_create_translation(QM_FILES ${SOURCES} ${TS_FILES} OPTIONS -locations none -no-obsolete)

set(PROJECT_SOURCES
    ${HEADERS}
    ${SOURCES}
    ${ICONS}
    ${WINICON}
    # ${QM_FILES}
    ${QHotkeySources}
)

# Add Mac icon
if (APPLE)
    FILE(GLOB_RECURSE MAC_SOURCES "*.mm")
    set(PROJECT_SOURCES
            ${PROJECT_SOURCES}
            ${MAC_SOURCES}
            ${CMAKE_SOURCE_DIR}/resources/icons/logo.icns
        )
    set(MACOSX_BUNDLE_ICON_FILE logo.icns)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/resources/icons/logo.icns" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    if (APPLE)
            set(MACOSX_BUNDLE TRUE)
            qt_add_executable(${TARGET_NAME} MACOSX_BUNDLE
                MANUAL_FINALIZATION
                ${PROJECT_SOURCES}
            )
    elseif (WIN32)
        qt_add_executable(${TARGET_NAME} WIN32
            MANUAL_FINALIZATION
            ${PROJECT_SOURCES}
        )
    endif()

else()
    if (APPLE)
        set(MACOSX_BUNDLE TRUE)
        add_executable(${TARGET_NAME} MACOSX_BUNDLE ${PROJECT_SOURCES})
    elseif (WIN32)
        add_executable(${TARGET_NAME} WIN32 ${PROJECT_SOURCES})
    endif()
endif()

set(TS_FILES ${PROJECT_SOURCE_DIR}/resources/languages/Capturer_en_US.ts ${PROJECT_SOURCE_DIR}/resources/languages/Capturer_zh_CN.ts)
qt_add_translations(${TARGET_NAME}
    TS_FILES ${TS_FILES}
    LUPDATE_OPTIONS -locations none -no-obsolete # 传给 lupdate 的参数放这里
)

target_compile_options(${TARGET_NAME}
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /Zc:preprocessor /Zc:__cplusplus /wd5054>
        $<$<CXX_COMPILER_ID:LINUX>:-Wall -Wextra -Wpedantic -Wno-deprecated-enum-enum-conversion>
        )

target_link_libraries(${TARGET_NAME}
    PRIVATE 
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
        # ${Glog_lib}
        fmt::fmt
        probe::probe
        ng-log::ng-log
        nlohmann_json::nlohmann_json
        $<$<PLATFORM_ID:Windows>:Winmm>
)
		
target_link_libraries(${TARGET_NAME}
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

if (APPLE)
    target_link_libraries(${TARGET_NAME}
        PRIVATE
        "-framework Carbon"
        "-framework Cocoa"
        "-framework AppKit"
        "-framework ServiceManagement"
        "-framework Foundation"
    )
endif()

if(APPLE)
    # 配置变量
    set(APP_NAME ${TARGET_NAME})
    set(APP_IDENTIFIER "com.jack.${TARGET_NAME}")
    set(APP_VERSION ${PROJECT_VERSION})
    set(APP_SHORT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})

    # 生成 Info.plist
    configure_file("${CMAKE_SOURCE_DIR}/resources/Info.plist" ${PROJECT_BINARY_DIR}/Info.plist @ONLY)

    set_target_properties(${TARGET_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_BINARY_DIR}/Info.plist
    )

endif()
		
target_include_directories(${TARGET_NAME}
    PRIVATE
        ./common
        ./widgets
        ./preview
        ./snipping
        ./player
        ./setting
        ${PROJECT_BINARY_DIR} # Generated header files
)

# set_target_properties(${TARGET_NAME} PROPERTIES
#     MACOSX_BUNDLE_GUI_IDENTIFIER com.jack.ScreenCapturer
#     MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
#     MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
# )

if(QT_VERSION_MAJOR EQUAL 6)
    # qt_import_qml_plugins(${TARGET_NAME})
    qt_finalize_executable(${TARGET_NAME})
endif()

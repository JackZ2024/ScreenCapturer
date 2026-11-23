
# FMT
# set(FMT ${PROJECT_SOURCE_DIR}/3rdparty/fmt)
# set(FMT_LIBPATH ${FMT}/lib)
# include_directories("${FMT}/include")
# if(CMAKE_BUILD_TYPE STREQUAL Debug)
    # set(FMT_lib ${FMT_LIBPATH}/fmtd.lib)
# else()
    # set(FMT_lib ${FMT_LIBPATH}/fmt.lib)
# endif()

# Glog
# set(Glog ${PROJECT_SOURCE_DIR}/3rdparty/glog)
# set(Glog_LIBPATH ${Glog}/lib)
# include_directories(${Glog}/include)
# if(CMAKE_BUILD_TYPE STREQUAL Debug)
    # set(Glog_lib ${Glog_LIBPATH}/glogd.lib)
# else()
    # set(Glog_lib ${Glog_LIBPATH}/glog.lib)
# endif()

# ng-log (managed by ng-log::ng-log target)
# set(NGlog ${PROJECT_SOURCE_DIR}/3rdparty/ng-log)
# include_directories(${NGlog}/include)

# Probe
# set(Probe ${PROJECT_SOURCE_DIR}/3rdparty/probe)
# set(Probe_LIBPATH ${Probe}/lib)
# include_directories("${Probe}/include")
# if(CMAKE_BUILD_TYPE STREQUAL Debug)
    # set(Probe_lib ${Probe_LIBPATH}/probed.lib)
# else()
    # set(Probe_lib ${Probe_LIBPATH}/probe.lib)
# endif()

# QHotkey
# set(QHotkey ${PROJECT_SOURCE_DIR}/3rdparty/QHotkey)
# set(QHotkey_LIBPATH ${QHotkey}/lib)
# include_directories("${QHotkey}/include")
# if(CMAKE_BUILD_TYPE STREQUAL Debug)
    # set(QHotkey_lib ${QHotkey_LIBPATH}/qhotkeyd.lib)
# else()
    # set(QHotkey_lib ${QHotkey_LIBPATH}/qhotkey.lib)
# endif()

# make up QHotkey
set(QHotkey_DIR ${CMAKE_CURRENT_LIST_DIR}/QHotkey)
# only for win32
if (APPLE)
	set(sysSources ${QHotkey_DIR}/qhotkey_mac.cpp)
elseif (WIN32)
	set(sysSources ${QHotkey_DIR}/qhotkey_win.cpp)
endif()
set(QHotkeySources
    ${QHotkey_DIR}/qhotkey.h
    ${QHotkey_DIR}/qhotkey.cpp
	${QHotkey_DIR}/qhotkey_p.h
	${sysSources}
)
# Include directory
include_directories(${QHotkey_DIR})

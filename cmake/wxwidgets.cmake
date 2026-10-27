# Locates wxWidgets and provides the imported target wx::wx.
#
# * If a wx-config script is available (Linux, MSYS2 shell, or cross builds via
#   -DwxWidgets_CONFIG_EXECUTABLE=...), it is queried directly.  This works for
#   MinGW cross compilation where FindwxWidgets is unreliable.
# * Otherwise CMake's FindwxWidgets is used (e.g. native Windows with
#   wxWidgets_ROOT_DIR).
set(WX_COMPONENTS aui core base)
set(WX_CONFIG_OPTIONS "" CACHE STRING "Extra options passed to wx-config (e.g. --static=yes)")

if(NOT wxWidgets_CONFIG_EXECUTABLE AND NOT (WIN32 AND NOT CMAKE_CROSSCOMPILING))
    find_program(wxWidgets_CONFIG_EXECUTABLE NAMES wx-config wx-config-3.2 wx-config-3.0)
endif()

add_library(wx::wx INTERFACE IMPORTED)

if(wxWidgets_CONFIG_EXECUTABLE)
    string(REPLACE ";" "," _wx_comps "${WX_COMPONENTS}")
    separate_arguments(_wx_opts UNIX_COMMAND "${WX_CONFIG_OPTIONS}")
    # Run through sh so that the script also works from a native Windows cmake.
    if(CMAKE_HOST_WIN32)
        set(_wx_cmd sh ${wxWidgets_CONFIG_EXECUTABLE})
    else()
        set(_wx_cmd ${wxWidgets_CONFIG_EXECUTABLE})
    endif()
    execute_process(COMMAND ${_wx_cmd} ${_wx_opts} --cxxflags
                    OUTPUT_VARIABLE _wx_cxxflags RESULT_VARIABLE _wx_rc1 OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${_wx_cmd} ${_wx_opts} --libs ${_wx_comps}
                    OUTPUT_VARIABLE _wx_libs RESULT_VARIABLE _wx_rc2 OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${_wx_cmd} ${_wx_opts} --version
                    OUTPUT_VARIABLE _wx_version OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _wx_rc1 EQUAL 0 OR NOT _wx_rc2 EQUAL 0)
        message(FATAL_ERROR "wx-config (${wxWidgets_CONFIG_EXECUTABLE}) failed")
    endif()
    separate_arguments(_wx_cxxflags UNIX_COMMAND "${_wx_cxxflags}")
    separate_arguments(_wx_libs UNIX_COMMAND "${_wx_libs}")
    set(_wx_incs "")
    set(_wx_defs "")
    set(_wx_other "")
    foreach(f IN LISTS _wx_cxxflags)
        if(f MATCHES "^-I(.*)")
            list(APPEND _wx_incs "${CMAKE_MATCH_1}")
        elseif(f MATCHES "^-D(.*)")
            list(APPEND _wx_defs "${CMAKE_MATCH_1}")
        else()
            list(APPEND _wx_other "${f}")
        endif()
    endforeach()
    # The executable is already marked WIN32; drop subsystem flags from wx-config.
    list(REMOVE_ITEM _wx_libs "-mwindows" "-Wl,--subsystem,windows")
    set_target_properties(wx::wx PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_wx_incs}"
        INTERFACE_COMPILE_DEFINITIONS "${_wx_defs}"
        INTERFACE_COMPILE_OPTIONS "${_wx_other}"
        INTERFACE_LINK_LIBRARIES "${_wx_libs}")
    message(STATUS "wxWidgets ${_wx_version} via ${wxWidgets_CONFIG_EXECUTABLE}")
else()
    find_package(wxWidgets 3.0 REQUIRED COMPONENTS ${WX_COMPONENTS})
    include(${wxWidgets_USE_FILE})
    set_target_properties(wx::wx PROPERTIES INTERFACE_LINK_LIBRARIES "${wxWidgets_LIBRARIES}")
    message(STATUS "wxWidgets found via FindwxWidgets")
endif()

cmake_minimum_required(VERSION 3.16)
project(ModbusGui VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MODBUSGUI_STATIC_RUNTIME "Link libgcc/libstdc++/winpthread statically (MinGW)" ON)

# ---- libmodbus (bundled, static) -------------------------------------------
include(cmake/libmodbus.cmake)

# ---- wxWidgets ---------------------------------------------------------------
# For cross builds pass -DwxWidgets_CONFIG_EXECUTABLE=<prefix>/bin/wx-config
include(cmake/wxwidgets.cmake)

# ---- application -------------------------------------------------------------
set(APP_SOURCES
    src/App.cpp
    src/MainFrame.cpp
    src/ConnectionPanel.cpp
    src/DataFormat.cpp
    src/LogView.cpp
    src/MasterPanel.cpp
    src/MasterWorker.cpp
    src/SlavePanel.cpp
    src/SlaveServer.cpp
    src/ModbusUtil.cpp
)
if(WIN32)
    list(APPEND APP_SOURCES src/app.rc)
endif()

configure_file(cmake/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h)

add_executable(modbus_gui WIN32 ${APP_SOURCES})
# resources/ is on the include path so that app.rc finds modbus_gui.ico
target_include_directories(modbus_gui PRIVATE src resources ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(modbus_gui PRIVATE MODBUSGUI_VERSION="${PROJECT_VERSION}")
target_link_libraries(modbus_gui PRIVATE modbus wx::wx)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(modbus_gui PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(MINGW)
    target_link_libraries(modbus_gui PRIVATE ws2_32)
    if(MODBUSGUI_STATIC_RUNTIME)
        target_link_options(modbus_gui PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

# ---- install / packages ------------------------------------------------------
include(cmake/packaging.cmake)

# ---- tests (headless, wxBase only) ------------------------------------------
option(MODBUSGUI_BUILD_TESTS "Build headless core tests" OFF)
if(MODBUSGUI_BUILD_TESTS)
    add_executable(test_core tests/test_core.cpp src/DataFormat.cpp src/MasterWorker.cpp
                             src/SlaveServer.cpp src/ModbusUtil.cpp)
    target_include_directories(test_core PRIVATE src)
    target_link_libraries(test_core PRIVATE modbus wx::wx)
    if(MINGW)
        target_link_libraries(test_core PRIVATE ws2_32)
        target_link_options(test_core PRIVATE -mconsole)
        if(MODBUSGUI_STATIC_RUNTIME)
            target_link_options(test_core PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()
    enable_testing()
    add_test(NAME test_core COMMAND test_core)
endif()

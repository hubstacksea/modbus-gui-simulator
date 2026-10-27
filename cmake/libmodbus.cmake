# Builds the bundled libmodbus (third_party/libmodbus) as a static library.
include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckSymbolExists)
include(CheckFunctionExists)

set(LIBMODBUS_DIR ${CMAKE_SOURCE_DIR}/third_party/libmodbus)
set(LIBMODBUS_VERSION_MAJOR 3)
set(LIBMODBUS_VERSION_MINOR 1)
set(LIBMODBUS_VERSION_MICRO 11)
set(LIBMODBUS_VERSION "${LIBMODBUS_VERSION_MAJOR}.${LIBMODBUS_VERSION_MINOR}.${LIBMODBUS_VERSION_MICRO}")

set(LIBMODBUS_GEN_DIR ${CMAKE_BINARY_DIR}/libmodbus_gen)

if(WIN32)
    set(HAVE_WINSOCK2_H 1)
    set(HAVE_DECL_TIOCSRS485 0)
    set(HAVE_DECL_TIOCM_RTS 0)
    check_include_file(unistd.h HAVE_UNISTD_H)
else()
    check_include_file(arpa/inet.h HAVE_ARPA_INET_H)
    check_include_file(netinet/in.h HAVE_NETINET_IN_H)
    check_include_files("sys/types.h;netinet/in.h;netinet/ip.h" HAVE_NETINET_IP_H)
    check_include_file(sys/socket.h HAVE_SYS_SOCKET_H)
    check_include_file(unistd.h HAVE_UNISTD_H)
    check_include_file(termios.h HAVE_TERMIOS_H)
    check_include_file(linux/serial.h HAVE_LINUX_SERIAL_H)
    set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
    check_symbol_exists(accept4 "sys/socket.h" HAVE_ACCEPT4)
    check_symbol_exists(getaddrinfo "netdb.h" HAVE_GETADDRINFO)
    check_symbol_exists(gai_strerror "netdb.h" HAVE_GAI_STRERROR)
    check_symbol_exists(strlcpy "string.h" HAVE_STRLCPY)
    check_symbol_exists(TIOCSRS485 "sys/ioctl.h" HAVE_TIOCSRS485_SYM)
    check_symbol_exists(TIOCM_RTS "sys/ioctl.h" HAVE_TIOCM_RTS_SYM)
    unset(CMAKE_REQUIRED_DEFINITIONS)
    set(HAVE_DECL_TIOCSRS485 0)
    set(HAVE_DECL_TIOCM_RTS 0)
    if(HAVE_TIOCSRS485_SYM)
        set(HAVE_DECL_TIOCSRS485 1)
    endif()
    if(HAVE_TIOCM_RTS_SYM)
        set(HAVE_DECL_TIOCM_RTS 1)
    endif()
endif()

configure_file(${CMAKE_SOURCE_DIR}/cmake/libmodbus_config.h.in ${LIBMODBUS_GEN_DIR}/config.h)
configure_file(${LIBMODBUS_DIR}/src/modbus-version.h.in ${LIBMODBUS_GEN_DIR}/modbus-version.h @ONLY)

add_library(modbus STATIC
    ${LIBMODBUS_DIR}/src/modbus.c
    ${LIBMODBUS_DIR}/src/modbus-data.c
    ${LIBMODBUS_DIR}/src/modbus-rtu.c
    ${LIBMODBUS_DIR}/src/modbus-tcp.c
)
set_target_properties(modbus PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
target_include_directories(modbus
    PUBLIC  ${LIBMODBUS_DIR}/src ${LIBMODBUS_GEN_DIR}
)
if(WIN32)
    target_link_libraries(modbus PUBLIC ws2_32)
else()
    target_compile_definitions(modbus PRIVATE _GNU_SOURCE)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # modbus-tcp.c tests HAVE_* macros before including config.h
    target_compile_options(modbus PRIVATE -w -include ${LIBMODBUS_GEN_DIR}/config.h)
endif()

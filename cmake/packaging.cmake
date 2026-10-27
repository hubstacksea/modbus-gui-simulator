# Install layout and CPack configuration.
#   Linux  : cpack -G "DEB;RPM"   -> modbus-gui_<ver>_amd64.deb / modbus-gui-<ver>-1.x86_64.rpm
#   Windows: cpack -G 7Z          -> modbus-gui-<ver>-win64.7z
set(MODBUSGUI_MAINTAINER "Modbus GUI Maintainer <maintainer@example.com>"
    CACHE STRING "Package maintainer (Name <email>)")
set(MODBUSGUI_LICENSE "Unspecified" CACHE STRING "License string written into the packages")
set(MODBUSGUI_HOMEPAGE "" CACHE STRING "Project home page URL")

set(PKG_NAME modbus-gui)
# 0755 directories regardless of the builder's umask
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(LIBMODBUS_LICENSE_FILE ${CMAKE_SOURCE_DIR}/third_party/libmodbus/COPYING.LESSER)

if(WIN32)
    # Portable layout: everything in the archive root folder
    install(TARGETS modbus_gui RUNTIME DESTINATION .)
    install(FILES README.md DESTINATION .)
    install(FILES ${LIBMODBUS_LICENSE_FILE} DESTINATION licenses RENAME libmodbus-COPYING.LESSER.txt)
else()
    include(GNUInstallDirs)
    install(TARGETS modbus_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/modbus-gui.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    set(_icon_sizes 16 24 32 48 64 128 256 512)
    foreach(s IN LISTS _icon_sizes)
        install(FILES resources/icons/modbus-gui-${s}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${s}x${s}/apps
                RENAME ${PKG_NAME}.png)
    endforeach()
    install(FILES resources/icons/modbus-gui-128.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
            RENAME ${PKG_NAME}.png)
    install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${PKG_NAME})
    install(FILES ${LIBMODBUS_LICENSE_FILE} DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${PKG_NAME}
            RENAME libmodbus-COPYING.LESSER)
endif()

# ---- common -----------------------------------------------------------------
set(CPACK_PACKAGE_NAME ${PKG_NAME})
set(CPACK_PACKAGE_VENDOR "Modbus GUI")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modbus TCP/RTU master tool and slave simulator")
string(CONCAT CPACK_PACKAGE_DESCRIPTION
    "GUI tool built on libmodbus and wxWidgets. Polls and writes Modbus devices over TCP or RTU "
    "(FC01-06, 15, 16, 17, 22, 23, raw requests, unit id scan) and simulates Modbus slaves with "
    "editable registers, value simulation and fault injection.")
set(CPACK_PACKAGE_CONTACT ${MODBUSGUI_MAINTAINER})
set(CPACK_PACKAGE_ICON ${CMAKE_SOURCE_DIR}/resources/modbus_gui.ico)
set(CPACK_RESOURCE_FILE_README ${CMAKE_SOURCE_DIR}/README.md)
set(CPACK_STRIP_FILES ON)
set(CPACK_SOURCE_IGNORE_FILES "/build.*/;/\\\\.git/")
if(MODBUSGUI_HOMEPAGE)
    set(CPACK_PACKAGE_HOMEPAGE_URL ${MODBUSGUI_HOMEPAGE})
endif()

if(WIN32)
    set(CPACK_GENERATOR 7Z)
    set(CPACK_PACKAGE_FILE_NAME ${PKG_NAME}-${PROJECT_VERSION}-win64)
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
else()
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)

    # DEB
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION electronics)
    set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) # wx / gtk dependencies via dpkg-shlibdeps
    set(CPACK_DEBIAN_PACKAGE_RELEASE 1)

    # RPM (Requires are generated automatically by rpmbuild)
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_RELEASE 1)
    set(CPACK_RPM_PACKAGE_LICENSE ${MODBUSGUI_LICENSE})
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
    if(MODBUSGUI_HOMEPAGE)
        set(CPACK_RPM_PACKAGE_URL ${MODBUSGUI_HOMEPAGE})
    endif()
    # Do not claim ownership of directories that belong to the system
    set(_excl /usr/share/applications /usr/share/icons /usr/share/icons/hicolor /usr/share/pixmaps
              /usr/share/doc)
    foreach(s IN LISTS _icon_sizes)
        list(APPEND _excl /usr/share/icons/hicolor/${s}x${s} /usr/share/icons/hicolor/${s}x${s}/apps)
    endforeach()
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION ${_excl})
endif()

include(CPack)

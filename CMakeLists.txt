cmake_minimum_required(VERSION 3.20)
project(lxqt-personalizer
    VERSION 0.1.0
    DESCRIPTION "Create, preview and apply desktop themes for LXQt/Openbox"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # g_autoptr relies on __attribute__((cleanup))
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(LXP_BUILD_TESTS "Build the test suite" ON)
option(LXP_BUILD_GUI "Build the GTK 4 application" ON)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0>=2.76 gio-2.0>=2.76)
pkg_check_modules(LIBXML2 REQUIRED IMPORTED_TARGET libxml-2.0)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)
pkg_check_modules(PREVIEW REQUIRED IMPORTED_TARGET cairo pangocairo gdk-pixbuf-2.0 librsvg-2.0>=2.52)

add_compile_options(
    -Wall -Wextra -Wshadow -Wformat=2 -Wmissing-prototypes
    -Wstrict-prototypes -Wno-unused-parameter)

configure_file(config.h.in ${CMAKE_BINARY_DIR}/config.h)

# All programs land in bin/ so the GUI finds lxp next to itself.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(src/core)
add_subdirectory(src/preview)
add_subdirectory(src/cli)
if(LXP_BUILD_GUI)
    add_subdirectory(src/gui)
endif()

install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME})

if(LXP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

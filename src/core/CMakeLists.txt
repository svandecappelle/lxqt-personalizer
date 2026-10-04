add_library(lxp STATIC
    lxp-error.c
    lxp-paths.c
    lxp-fileutil.c
    lxp-theme.c
    lxp-backend.c
    lxp-snapshot.c
    backends/lxqt.c
    backends/openbox.c)

target_include_directories(lxp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR})

target_link_libraries(lxp PUBLIC PkgConfig::GLIB PkgConfig::LIBXML2)

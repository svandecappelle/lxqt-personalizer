add_library(lxp STATIC
    lxp-error.c
    lxp-paths.c
    lxp-fileutil.c
    lxp-ini.c
    lxp-system.c
    lxp-theme.c
    lxp-apply.c
    lxp-backend.c
    lxp-snapshot.c
    backends/lxqt.c
    backends/openbox.c
    backends/gtk.c
    backends/cursor.c
    backends/wallpaper.c
    backends/panel.c)

target_include_directories(lxp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR})

target_link_libraries(lxp PUBLIC PkgConfig::GLIB PkgConfig::LIBXML2)

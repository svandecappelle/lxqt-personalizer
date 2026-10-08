find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(TEMPLATES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/templates)
file(GLOB TEMPLATE_FILES ${TEMPLATES_DIR}/*)
list(FILTER TEMPLATE_FILES EXCLUDE REGEX "\\.xml$")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lxp-templates.c
    COMMAND ${GLIB_COMPILE_RESOURCES} --generate-source
            --c-name lxp_templates --manual-register
            --sourcedir=${TEMPLATES_DIR}
            --target=${CMAKE_CURRENT_BINARY_DIR}/lxp-templates.c
            ${TEMPLATES_DIR}/templates.gresource.xml
    DEPENDS ${TEMPLATES_DIR}/templates.gresource.xml ${TEMPLATE_FILES}
    VERBATIM)

add_library(lxp STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/lxp-templates.c
    lxp-assets.c
    lxp-recolor.c
    lxp-scheme.c
    lxp-template.c
    lxp-error.c
    lxp-paths.c
    lxp-fileutil.c
    lxp-ini.c
    lxp-system.c
    lxp-theme.c
    lxp-themerc.c
    lxp-apply.c
    lxp-backend.c
    lxp-color.c
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

target_link_libraries(lxp PUBLIC PkgConfig::GLIB PkgConfig::LIBXML2 m)

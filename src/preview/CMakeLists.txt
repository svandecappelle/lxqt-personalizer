add_library(lxp-preview STATIC
    lxp-icons.c
    lxp-wallgen.c
    lxp-palette.c
    lxp-themegen.c
    lxp-image.c
    lxp-preview-model.c
    lxp-preview-render.c)

target_include_directories(lxp-preview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lxp-preview PUBLIC lxp PkgConfig::PREVIEW m)

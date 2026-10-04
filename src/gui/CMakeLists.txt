pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.12)

add_executable(lxqt-personalizer
    main.c
    lxp-preview-area.c
    lxp-runner.c
    lxp-window.c)

target_link_libraries(lxqt-personalizer PRIVATE lxp lxp-preview PkgConfig::GTK4)

install(TARGETS lxqt-personalizer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/io.github.lxqt_personalizer.LxqtPersonalizer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

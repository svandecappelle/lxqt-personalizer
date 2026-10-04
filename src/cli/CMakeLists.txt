add_executable(lxp-cli main.c)
set_target_properties(lxp-cli PROPERTIES OUTPUT_NAME lxp)
target_link_libraries(lxp-cli PRIVATE lxp lxp-preview)

install(TARGETS lxp-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

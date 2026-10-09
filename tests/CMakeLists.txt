foreach(name ini color theme backends snapshot apply pack)
    add_executable(test-${name} test-${name}.c)
    target_link_libraries(test-${name} PRIVATE lxp)
    target_compile_definitions(test-${name} PRIVATE
        TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME ${name} COMMAND test-${name})
endforeach()

# Tests that also need the preview library
foreach(name preview generate compositor tier1 tier3)
    add_executable(test-${name} test-${name}.c)
    target_link_libraries(test-${name} PRIVATE lxp-preview)
    target_compile_definitions(test-${name} PRIVATE
        TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME ${name} COMMAND test-${name})
endforeach()

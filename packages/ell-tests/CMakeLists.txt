include(Catch)

add_executable(
    ell-tests
    src/expression_tests.cpp
    src/conformance_tests.cpp
    src/lexer_parser_tests.cpp
    src/render_tests.cpp
    src/source_tests.cpp
    src/version_tests.cpp
)

target_link_libraries(
    ell-tests
    PRIVATE
        ell::core
        Catch2::Catch2WithMain
)

target_compile_features(ell-tests PRIVATE cxx_std_23)
target_compile_definitions(ell-tests PRIVATE ELL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

catch_discover_tests(ell-tests)

add_test(
    NAME ellc-data-transports
    COMMAND "${CMAKE_COMMAND}"
            -DELLC=$<TARGET_FILE:ellc>
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cli_contract.cmake
)

add_test(
    NAME installed-prefix-smoke
    COMMAND "${CMAKE_COMMAND}"
            -DELLC=$<TARGET_FILE:ellc>
            -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DINSTALL_DIR=${CMAKE_BINARY_DIR}/install-smoke
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/install_contract.cmake
)

add_test(
    NAME ell-lsp-protocol
    COMMAND "${CMAKE_COMMAND}"
            -DELL_LSP=$<TARGET_FILE:ell-lsp>
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lsp_contract.cmake
)

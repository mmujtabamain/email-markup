include(Catch)

add_executable(
    email-markup-tests
    src/expression_tests.cpp
    src/image_tests.cpp
    src/conformance_tests.cpp
    src/css_lint_tests.cpp
    src/lexer_parser_tests.cpp
    src/lsp_support_tests.cpp
    src/platform_tests.cpp
    src/render_tests.cpp
    src/source_tests.cpp
    src/version_tests.cpp
)

target_link_libraries(
    email-markup-tests
    PRIVATE
        email_markup::core
        email_markup::platform
        email-markup-lsp-support
        Catch2::Catch2WithMain
)

target_compile_features(email-markup-tests PRIVATE cxx_std_23)
target_compile_definitions(
    email-markup-tests
    PRIVATE
        EMAIL_MARKUP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        EMAIL_MARKUP_VERSION="${PROJECT_VERSION}"
)

catch_discover_tests(email-markup-tests)

add_test(
    NAME emc-data-transports
    COMMAND "${CMAKE_COMMAND}"
            -DEMC=$<TARGET_FILE:emc>
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cli_contract.cmake
)

add_test(
    NAME installed-prefix-smoke
    COMMAND "${CMAKE_COMMAND}"
            -DEMC=$<TARGET_FILE:emc>
            -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DINSTALL_DIR=${CMAKE_BINARY_DIR}/install-smoke
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/install_contract.cmake
)

add_test(
    NAME email-markup-lsp-protocol
    COMMAND "${CMAKE_COMMAND}"
            -DEMAIL_MARKUP_LSP=$<TARGET_FILE:email-markup-lsp>
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lsp_contract.cmake
)

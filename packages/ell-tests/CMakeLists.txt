include(Catch)

add_executable(
    ell-tests
    src/expression_tests.cpp
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

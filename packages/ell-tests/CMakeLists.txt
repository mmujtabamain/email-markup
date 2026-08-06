include(Catch)

add_executable(
    ell-tests
    src/version_tests.cpp
)

target_link_libraries(
    ell-tests
    PRIVATE
        ell::core
        Catch2::Catch2WithMain
)

target_compile_features(ell-tests PRIVATE cxx_std_23)

catch_discover_tests(ell-tests)

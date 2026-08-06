add_executable(
    ell-lsp
    src/main.cpp
)

target_link_libraries(
    ell-lsp
    PRIVATE
        ell::core
        fmt::fmt
)

target_compile_features(ell-lsp PRIVATE cxx_std_23)

install(TARGETS ell-lsp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

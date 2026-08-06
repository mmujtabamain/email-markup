add_executable(
    ellc
    src/main.cpp
)

target_link_libraries(
    ellc
    PRIVATE
        ell::core
        fmt::fmt
)

target_compile_features(ellc PRIVATE cxx_std_23)

install(TARGETS ellc RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

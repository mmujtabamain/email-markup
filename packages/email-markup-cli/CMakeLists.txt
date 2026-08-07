add_executable(
    emc
    src/main.cpp
)

target_link_libraries(
    emc
    PRIVATE
        email_markup::core
        fmt::fmt
)

target_compile_features(emc PRIVATE cxx_std_23)

install(TARGETS emc RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

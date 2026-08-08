add_executable(
    emc
    src/app/application.cpp
    src/commands/command_executor.cpp
    src/commands/command_line.cpp
    src/compilation/compilation_session.cpp
    src/diagnostics/diagnostic_reporter.cpp
    src/main.cpp
)

target_include_directories(emc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(
    emc
    PRIVATE
        email_markup::core
        email_markup::platform
        email_markup::runtime
        fmt::fmt-header-only
)

target_compile_features(emc PRIVATE cxx_std_23)

install(TARGETS emc RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

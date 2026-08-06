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

if(APPLE)
    set(ELL_VSCODE_PLATFORM "darwin")
elseif(WIN32)
    set(ELL_VSCODE_PLATFORM "win32")
else()
    set(ELL_VSCODE_PLATFORM "linux")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ELL_PROCESSOR)
if(ELL_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(ELL_VSCODE_ARCH "arm64")
else()
    set(ELL_VSCODE_ARCH "x64")
endif()

set(ELL_VSCODE_SERVER_DIR
    "${CMAKE_SOURCE_DIR}/extensions/vscode/server/${ELL_VSCODE_PLATFORM}-${ELL_VSCODE_ARCH}")
add_custom_command(
    TARGET ell-lsp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ELL_VSCODE_SERVER_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:ell-lsp>" "${ELL_VSCODE_SERVER_DIR}/"
    COMMENT "Staging ell-lsp for the local VS Code extension package"
)

install(TARGETS ell-lsp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_executable(
    email-markup-lsp
    src/main.cpp
)

target_link_libraries(
    email-markup-lsp
    PRIVATE
        email_markup::core
        fmt::fmt
)

target_compile_features(email-markup-lsp PRIVATE cxx_std_23)

if(APPLE)
    set(EMAIL_MARKUP_VSCODE_PLATFORM "darwin")
elseif(WIN32)
    set(EMAIL_MARKUP_VSCODE_PLATFORM "win32")
else()
    set(EMAIL_MARKUP_VSCODE_PLATFORM "linux")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" EMAIL_MARKUP_PROCESSOR)
if(EMAIL_MARKUP_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(EMAIL_MARKUP_VSCODE_ARCH "arm64")
else()
    set(EMAIL_MARKUP_VSCODE_ARCH "x64")
endif()

set(EMAIL_MARKUP_VSCODE_SERVER_DIR
    "${CMAKE_SOURCE_DIR}/extensions/vscode/server/${EMAIL_MARKUP_VSCODE_PLATFORM}-${EMAIL_MARKUP_VSCODE_ARCH}")
add_custom_command(
    TARGET email-markup-lsp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EMAIL_MARKUP_VSCODE_SERVER_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:email-markup-lsp>" "${EMAIL_MARKUP_VSCODE_SERVER_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/lib" "${EMAIL_MARKUP_VSCODE_SERVER_DIR}/lib"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/brand" "${EMAIL_MARKUP_VSCODE_SERVER_DIR}/brand"
    COMMENT "Staging email-markup-lsp and its runtime assets for the VS Code extension package"
)

install(TARGETS email-markup-lsp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

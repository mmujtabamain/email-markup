add_library(
    email-markup-lsp-support
    src/analysis/context.cpp
    src/documents/document_store.cpp
    src/features/completion.cpp
    src/features/document.cpp
    src/features/navigation.cpp
    src/protocol/json_rpc.cpp
    src/server/server.cpp
    src/text/positions.cpp
    src/workspace/workspace.cpp
)

target_include_directories(
    email-markup-lsp-support
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
    email-markup-lsp-support
    PUBLIC
        email_markup::core
        email_markup::platform
        email_markup::runtime
)

target_compile_features(email-markup-lsp-support PUBLIC cxx_std_23)

add_executable(
    email-markup-lsp
    src/main.cpp
)

target_link_libraries(email-markup-lsp PRIVATE email-markup-lsp-support)
target_compile_features(email-markup-lsp PRIVATE cxx_std_23)

get_target_property(
    EMAIL_MARKUP_VSCODE_PLATFORM
    email-markup-platform
    EMAIL_MARKUP_VSCODE_PLATFORM
)

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
    COMMENT "Staging email-markup-lsp and its runtime assets for the VS Code extension package"
)

install(TARGETS email-markup-lsp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

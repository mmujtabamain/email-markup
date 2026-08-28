cmake_minimum_required(VERSION 4.3)

if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING
        "Use self-contained Windows release binaries")
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" EMAIL_MARKUP_VERSION)
string(STRIP "${EMAIL_MARKUP_VERSION}" EMAIL_MARKUP_VERSION)
if(NOT EMAIL_MARKUP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic version in X.Y.Z form")
endif()

project(
    EmailMarkup
    VERSION "${EMAIL_MARKUP_VERSION}"
    DESCRIPTION "A typed, component-based language for HTML email"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(EMAIL_MARKUP_SANITIZERS "" CACHE STRING "Comma-separated sanitizers for development builds")
option(EMAIL_MARKUP_BROWSER_ONLY
       "Build only the portable compiler core and browser/WASM surface" OFF)
if(EMAIL_MARKUP_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options("-fsanitize=${EMAIL_MARKUP_SANITIZERS}" -fno-omit-frame-pointer)
    add_link_options("-fsanitize=${EMAIL_MARKUP_SANITIZERS}" -fno-omit-frame-pointer)
endif()

include(GNUInstallDirs)
include(CTest)

find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(packages/email-markup-core)
add_subdirectory(packages/email-markup-browser)

if(EMAIL_MARKUP_BROWSER_ONLY)
    return()
endif()

find_package(Catch2 CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_subdirectory(packages/email-markup-platform)
add_subdirectory(packages/email-markup-runtime)
add_subdirectory(packages/email-markup-cli)
add_subdirectory(packages/email-markup-lsp)
add_subdirectory(packages/email-markup-tests)

add_custom_target(
    email-markup-dev-assets ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/lib"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/schema"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/schema"
    COMMENT "Copying Email Markup runtime assets"
)

add_dependencies(emc email-markup-dev-assets)
add_dependencies(email-markup-lsp email-markup-dev-assets)

install(DIRECTORY lib/ DESTINATION "${CMAKE_INSTALL_DATADIR}/email-markup/lib")
install(DIRECTORY schema/ DESTINATION "${CMAKE_INSTALL_DATADIR}/email-markup/schema")
install(DIRECTORY grammar/ DESTINATION "${CMAKE_INSTALL_DATADIR}/email-markup/grammar")
install(DIRECTORY syntax/ DESTINATION "${CMAKE_INSTALL_DATADIR}/email-markup/syntax")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DATADIR}/email-markup/docs")
install(FILES README.md EMAIL-MARKUP.md templates.md LICENSE
        DESTINATION "${CMAKE_INSTALL_DATADIR}/email-markup")
install(EXPORT email-markup-targets NAMESPACE email_markup::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/email-markup")

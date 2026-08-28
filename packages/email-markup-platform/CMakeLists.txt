set(EMAIL_MARKUP_PLATFORM_SOURCE src/os/portable.cpp)
set(EMAIL_MARKUP_VSCODE_PLATFORM "linux")

if(APPLE)
    set(EMAIL_MARKUP_PLATFORM_SOURCE src/os/macos.cpp)
    set(EMAIL_MARKUP_VSCODE_PLATFORM "darwin")
elseif(WIN32)
    set(EMAIL_MARKUP_PLATFORM_SOURCE src/os/windows.cpp)
    set(EMAIL_MARKUP_VSCODE_PLATFORM "win32")
elseif(UNIX)
    set(EMAIL_MARKUP_PLATFORM_SOURCE src/os/posix.cpp)
endif()

add_library(
    email-markup-platform
    src/system.cpp
    ${EMAIL_MARKUP_PLATFORM_SOURCE}
)

add_library(email_markup::platform ALIAS email-markup-platform)

target_include_directories(
    email-markup-platform
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(email-markup-platform PUBLIC cxx_std_23)
target_link_libraries(email-markup-platform PUBLIC CURL::libcurl)

set_target_properties(
    email-markup-platform
    PROPERTIES
        OUTPUT_NAME email-markup-platform
        EMAIL_MARKUP_VSCODE_PLATFORM "${EMAIL_MARKUP_VSCODE_PLATFORM}"
)

install(
    TARGETS email-markup-platform
    EXPORT email-markup-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

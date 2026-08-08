add_library(
    email-markup-runtime
    src/assets.cpp
)

add_library(email_markup::runtime ALIAS email-markup-runtime)

target_include_directories(
    email-markup-runtime
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(email-markup-runtime PUBLIC email_markup::platform)
target_compile_features(email-markup-runtime PUBLIC cxx_std_23)

set_target_properties(email-markup-runtime PROPERTIES OUTPUT_NAME email-markup-runtime)

install(
    TARGETS email-markup-runtime
    EXPORT email-markup-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

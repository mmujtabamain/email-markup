add_library(
    email-markup-core
    src/css.cpp
    src/compilation/compiler.cpp
    src/compilation/loader.cpp
    src/compilation/tokens.cpp
    src/context_schema.cpp
    src/data.cpp
    src/diagnostic.cpp
    src/engine.cpp
    src/emir.cpp
    src/expr.cpp
    src/format.cpp
    src/include.cpp
    src/images.cpp
    src/lexer.cpp
    src/lint.cpp
    src/parser.cpp
    src/provenance.cpp
    src/registry.cpp
    src/rendering/renderer.cpp
    src/source.cpp
    src/styles.cpp
    src/types.cpp
    src/version.cpp
)

add_library(email_markup::core ALIAS email-markup-core)

target_include_directories(
    email-markup-core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(email-markup-core PUBLIC nlohmann_json::nlohmann_json)

target_compile_features(email-markup-core PUBLIC cxx_std_23)
target_compile_definitions(
    email-markup-core
    PRIVATE EMAIL_MARKUP_VERSION="${PROJECT_VERSION}"
)

set_target_properties(email-markup-core PROPERTIES OUTPUT_NAME email-markup-core)

install(
    TARGETS email-markup-core
    EXPORT email-markup-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

add_library(
    ell-core
    src/version.cpp
)

add_library(ell::core ALIAS ell-core)

target_include_directories(
    ell-core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(ell-core PUBLIC nlohmann_json::nlohmann_json)

target_compile_features(ell-core PUBLIC cxx_std_23)

set_target_properties(ell-core PROPERTIES OUTPUT_NAME ell-core)

install(
    TARGETS ell-core
    EXPORT ell-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

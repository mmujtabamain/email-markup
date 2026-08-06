cmake_minimum_required(VERSION 4.0)

project(
    ell
    VERSION 0.1.0
    DESCRIPTION "The ELL email layout language"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(GNUInstallDirs)
include(CTest)

find_package(Catch2 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(packages/ell-core)
add_subdirectory(packages/ell-cli)
add_subdirectory(packages/ell-lsp)
add_subdirectory(packages/ell-tests)

add_custom_target(
    ell-dev-assets ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/lib"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/brand"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/brand"
    COMMENT "Copying ELL libraries and brand assets"
)

add_dependencies(ellc ell-dev-assets)
add_dependencies(ell-lsp ell-dev-assets)

install(DIRECTORY lib/ DESTINATION "${CMAKE_INSTALL_DATADIR}/ell/lib")
install(DIRECTORY brand/ DESTINATION "${CMAKE_INSTALL_DATADIR}/ell/brand")

add_library(
    email-markup-browser
    src/protocol.cpp
)

add_library(email_markup::browser ALIAS email-markup-browser)

target_include_directories(
    email-markup-browser
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(email-markup-browser PUBLIC email_markup::core)
target_compile_features(email-markup-browser PUBLIC cxx_std_23)

set_target_properties(email-markup-browser PROPERTIES OUTPUT_NAME email-markup-browser)

install(
    TARGETS email-markup-browser
    EXPORT email-markup-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(EMSCRIPTEN)
    add_executable(email-markup-browser-wasm src/wasm_entry.cpp)
    target_link_libraries(email-markup-browser-wasm PRIVATE email_markup::browser)
    target_compile_features(email-markup-browser-wasm PRIVATE cxx_std_23)
    set_target_properties(
        email-markup-browser-wasm
        PROPERTIES
            OUTPUT_NAME "email-markup-browser"
            SUFFIX ".mjs"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/browser"
    )
    target_link_options(
        email-markup-browser-wasm
        PRIVATE
            "-sWASM=1"
            "-sMODULARIZE=1"
            "-sEXPORT_ES6=1"
            "-sENVIRONMENT=worker"
            "-sFILESYSTEM=0"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sINITIAL_MEMORY=33554432"
            "-sMAXIMUM_MEMORY=268435456"
            "-sNO_EXIT_RUNTIME=1"
            "-sDISABLE_EXCEPTION_CATCHING=0"
            # Without these an exception that escapes to JavaScript arrives as a
            # bare pointer, and the editor can only report the number it was
            # given. The helpers let the worker turn it back into a type and a
            # message.
            "-sEXPORT_EXCEPTION_HANDLING_HELPERS=1"
            # The compiler is recursive, so a cyclic project reaches the stack
            # limit before anything else. The default 64 KiB stack turns that
            # into an unexplained abort; a larger stack with overflow checking
            # turns it into a diagnosable error.
            "-sSTACK_SIZE=4194304"
            "-sSTACK_OVERFLOW_CHECK=1"
            # `ccall` copies a string argument onto the *stack*, so the request
            # size was bounded by the stack rather than by the protocol: a
            # request over ~64 KiB faulted with "memory access out of bounds",
            # reported to the author as an unexplained WebAssembly exception.
            # Exporting the allocator and the string helpers lets the worker put
            # the request on the heap instead, where the only limit is the
            # protocol's own 1 MiB.
            "-sEXPORTED_FUNCTIONS=['_email_markup_browser_request','_email_markup_browser_protocol_version','_malloc','_free']"
            "-sEXPORTED_RUNTIME_METHODS=['ccall','getExceptionMessage','stringToUTF8','lengthBytesUTF8','UTF8ToString','HEAPU8']"
    )
endif()

set(WORK_DIR "${BINARY_DIR}/cli-contract")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(EXAMPLE_DIR "${SOURCE_DIR}/examples/01-interpolation")
file(READ "${EXAMPLE_DIR}/data.json" DATA_JSON)

execute_process(
  COMMAND "${EMC}" compile "${EXAMPLE_DIR}/message.em"
          --data-file "${EXAMPLE_DIR}/data.json"
          -o "${WORK_DIR}/file.html"
  RESULT_VARIABLE FILE_RESULT
  ERROR_VARIABLE FILE_ERROR
)
if(NOT FILE_RESULT EQUAL 0)
  message(FATAL_ERROR "file JSON compile failed: ${FILE_ERROR}")
endif()

execute_process(
  COMMAND "${EMC}" compile "${EXAMPLE_DIR}/message.em"
          --data-json "${DATA_JSON}"
          -o "${WORK_DIR}/direct.html"
  RESULT_VARIABLE DIRECT_RESULT
  ERROR_VARIABLE DIRECT_ERROR
)
if(NOT DIRECT_RESULT EQUAL 0)
  message(FATAL_ERROR "direct JSON compile failed: ${DIRECT_ERROR}")
endif()

execute_process(
  COMMAND "${EMC}" compile "${EXAMPLE_DIR}/message.em"
          --data-stdin -o "${WORK_DIR}/stdin.html"
  INPUT_FILE "${EXAMPLE_DIR}/data.json"
  RESULT_VARIABLE STDIN_RESULT
  ERROR_VARIABLE STDIN_ERROR
)
if(NOT STDIN_RESULT EQUAL 0)
  message(FATAL_ERROR "stdin JSON compile failed: ${STDIN_ERROR}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${WORK_DIR}/file.html" "${WORK_DIR}/direct.html"
  RESULT_VARIABLE DIRECT_COMPARE
)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${WORK_DIR}/file.html" "${WORK_DIR}/stdin.html"
  RESULT_VARIABLE STDIN_COMPARE
)
if(NOT DIRECT_COMPARE EQUAL 0 OR NOT STDIN_COMPARE EQUAL 0)
  message(FATAL_ERROR "JSON transports produced different HTML")
endif()

set(PROTOCOL_REQUEST
    "{\"protocol\":\"email-markup.compile\",\"version\":1,\"entry_path\":\"/message.em\",\"source\":\"@Include(\\\"card.em\\\"); @Card @/Card\",\"files\":[{\"path\":\"/library/card.em\",\"source\":\"@DefineComponent(name: \\\"Card\\\") @Template <p>Hello @{business.name}</p> @/Template @/DefineComponent\"}],\"include_directories\":[\"/library\"],\"imports\":[],\"shell\":{\"path\":\"/shell.em\",\"source\":\"<!doctype html><html><body>@Slot(default); <a href=\\\"@{unsubscribe_url}\\\">Unsubscribe</a></body></html>\"},\"recipient\":{\"business\":{\"name\":\"Acme & Co\"},\"unsubscribe_url\":\"https://example.test/unsubscribe\"}}")
file(WRITE "${WORK_DIR}/request.json" "${PROTOCOL_REQUEST}")
execute_process(
  COMMAND "${EMC}" compile --request-stdin
  INPUT_FILE "${WORK_DIR}/request.json"
  RESULT_VARIABLE PROTOCOL_RESULT
  OUTPUT_VARIABLE PROTOCOL_OUTPUT
  ERROR_VARIABLE PROTOCOL_ERROR
)
if(NOT PROTOCOL_RESULT EQUAL 0 OR
   NOT PROTOCOL_OUTPUT MATCHES "\\\"success\\\":true" OR
   NOT PROTOCOL_OUTPUT MATCHES "Acme &amp; Co" OR
   NOT PROTOCOL_OUTPUT MATCHES "\\\"/library/card.em\\\"")
  message(FATAL_ERROR "request protocol compile failed: ${PROTOCOL_ERROR}\n${PROTOCOL_OUTPUT}")
endif()

file(WRITE "${WORK_DIR}/bad-request.json"
     "{\"protocol\":\"email-markup.compile\",\"version\":99}")
execute_process(
  COMMAND "${EMC}" compile --request-stdin
  INPUT_FILE "${WORK_DIR}/bad-request.json"
  RESULT_VARIABLE BAD_PROTOCOL_RESULT
  OUTPUT_VARIABLE BAD_PROTOCOL_OUTPUT
)
if(NOT BAD_PROTOCOL_RESULT EQUAL 2 OR
   NOT BAD_PROTOCOL_OUTPUT MATCHES "\\\"code\\\":\\\"EMPROTO\\\"")
  message(FATAL_ERROR "invalid protocol input did not return JSON and exit 2")
endif()

set(COMPILATION_ERROR_REQUEST
    "{\"protocol\":\"email-markup.compile\",\"version\":1,\"entry_path\":\"/message.em\",\"source\":\"@Missing @/Missing\",\"recipient\":{}}")
file(WRITE "${WORK_DIR}/compilation-error-request.json" "${COMPILATION_ERROR_REQUEST}")
execute_process(
  COMMAND "${EMC}" compile --request-stdin
  INPUT_FILE "${WORK_DIR}/compilation-error-request.json"
  RESULT_VARIABLE COMPILATION_ERROR_RESULT
  OUTPUT_VARIABLE COMPILATION_ERROR_OUTPUT
)
if(NOT COMPILATION_ERROR_RESULT EQUAL 1 OR
   NOT COMPILATION_ERROR_OUTPUT MATCHES "\\\"success\\\":false" OR
   NOT COMPILATION_ERROR_OUTPUT MATCHES "EM0720")
  message(FATAL_ERROR "compiler findings did not return JSON and exit 1")
endif()

string(REPEAT "x" 1048577 OVERSIZED_REQUEST)
file(WRITE "${WORK_DIR}/oversized-request.json" "${OVERSIZED_REQUEST}")
execute_process(
  COMMAND "${EMC}" compile --request-stdin
  INPUT_FILE "${WORK_DIR}/oversized-request.json"
  RESULT_VARIABLE OVERSIZED_RESULT
  OUTPUT_VARIABLE OVERSIZED_OUTPUT
)
if(NOT OVERSIZED_RESULT EQUAL 2 OR
   NOT OVERSIZED_OUTPUT MATCHES "1 MiB protocol limit")
  message(FATAL_ERROR "oversized protocol input was not rejected with exit 2")
endif()

execute_process(
  COMMAND "${EMC}" compile "${EXAMPLE_DIR}/message.em"
          --data-json "{}" --data-file "${EXAMPLE_DIR}/data.json"
          -o "${WORK_DIR}/conflict.html"
  RESULT_VARIABLE CONFLICT_RESULT
  OUTPUT_QUIET ERROR_QUIET
)
if(CONFLICT_RESULT EQUAL 0)
  message(FATAL_ERROR "conflicting JSON sources were accepted")
endif()

file(WRITE "${WORK_DIR}/invalid.em" "@{secret.value}\n")
file(WRITE "${WORK_DIR}/preserved.html" "previous-output")
execute_process(
  COMMAND "${EMC}" compile "${WORK_DIR}/invalid.em"
          --data-json "{\"secret\":{\"other\":\"secret-value-that-must-not-leak\"}}" --json
          -o "${WORK_DIR}/preserved.html"
  RESULT_VARIABLE INVALID_RESULT
  OUTPUT_VARIABLE INVALID_OUTPUT
  ERROR_VARIABLE INVALID_ERROR
)
if(INVALID_RESULT EQUAL 0)
  message(FATAL_ERROR "missing JSON path was accepted")
endif()
file(READ "${WORK_DIR}/preserved.html" PRESERVED_OUTPUT)
if(NOT PRESERVED_OUTPUT STREQUAL "previous-output")
  message(FATAL_ERROR "failed compilation replaced an existing output")
endif()
if(INVALID_OUTPUT MATCHES "secret-value-that-must-not-leak")
  message(FATAL_ERROR "diagnostics leaked recipient data")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}/project")
file(WRITE "${WORK_DIR}/project/em.json"
     "{\"data\":\"data.json\",\"out\":\"out\"}")
file(WRITE "${WORK_DIR}/project/data.json" "{}")
file(WRITE "${WORK_DIR}/project/plain.em" "<p>Hello</p>\n")
execute_process(
  COMMAND "${EMC}" build "${WORK_DIR}/project"
  RESULT_VARIABLE BUILD_RESULT
  ERROR_VARIABLE BUILD_ERROR
)
if(NOT BUILD_RESULT EQUAL 0 OR
   NOT EXISTS "${WORK_DIR}/project/out/plain.html")
  message(FATAL_ERROR "emc build failed: ${BUILD_ERROR}")
endif()

execute_process(
  COMMAND "${EMC}" check "${WORK_DIR}/project/plain.em"
  RESULT_VARIABLE CHECK_RESULT
  ERROR_VARIABLE CHECK_ERROR
)
if(NOT CHECK_RESULT EQUAL 0)
  message(FATAL_ERROR "emc check failed: ${CHECK_ERROR}")
endif()

# file(WRITE) uses text mode, so a native newline becomes CRLF on Windows.
file(WRITE "${WORK_DIR}/format.em" "<p>trailing   \n")
execute_process(
  COMMAND "${EMC}" fmt "${WORK_DIR}/format.em" --write
  RESULT_VARIABLE FORMAT_RESULT
)
file(READ "${WORK_DIR}/format.em" FORMATTED)
if(NOT FORMAT_RESULT EQUAL 0 OR NOT FORMATTED STREQUAL "<p>trailing\n")
  message(FATAL_ERROR "emc fmt --write failed")
endif()

execute_process(
  COMMAND "${EMC}" schema
  RESULT_VARIABLE SCHEMA_RESULT
  OUTPUT_VARIABLE SCHEMA_OUTPUT
)
if(NOT SCHEMA_RESULT EQUAL 0 OR NOT SCHEMA_OUTPUT MATCHES "Email Markup project configuration")
  message(FATAL_ERROR "emc schema failed")
endif()

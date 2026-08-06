set(WORK_DIR "${BINARY_DIR}/cli-contract")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(READ "${SOURCE_DIR}/examples/solution_first.json" DATA_JSON)

execute_process(
  COMMAND "${ELLC}" compile "${SOURCE_DIR}/examples/solution_first.ell"
          --data-file "${SOURCE_DIR}/examples/solution_first.json"
          -o "${WORK_DIR}/file.html"
  RESULT_VARIABLE FILE_RESULT
  ERROR_VARIABLE FILE_ERROR
)
if(NOT FILE_RESULT EQUAL 0)
  message(FATAL_ERROR "file JSON compile failed: ${FILE_ERROR}")
endif()

execute_process(
  COMMAND "${ELLC}" compile "${SOURCE_DIR}/examples/solution_first.ell"
          --data-json "${DATA_JSON}"
          -o "${WORK_DIR}/direct.html"
  RESULT_VARIABLE DIRECT_RESULT
  ERROR_VARIABLE DIRECT_ERROR
)
if(NOT DIRECT_RESULT EQUAL 0)
  message(FATAL_ERROR "direct JSON compile failed: ${DIRECT_ERROR}")
endif()

execute_process(
  COMMAND "${ELLC}" compile "${SOURCE_DIR}/examples/solution_first.ell"
          --data-stdin -o "${WORK_DIR}/stdin.html"
  INPUT_FILE "${SOURCE_DIR}/examples/solution_first.json"
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

execute_process(
  COMMAND "${ELLC}" compile "${SOURCE_DIR}/examples/solution_first.ell"
          --data-json "{}" --data-file "${SOURCE_DIR}/examples/solution_first.json"
          -o "${WORK_DIR}/conflict.html"
  RESULT_VARIABLE CONFLICT_RESULT
  OUTPUT_QUIET ERROR_QUIET
)
if(CONFLICT_RESULT EQUAL 0)
  message(FATAL_ERROR "conflicting JSON sources were accepted")
endif()

file(WRITE "${WORK_DIR}/invalid.ell" "@{secret.value}\n")
file(WRITE "${WORK_DIR}/preserved.html" "previous-output")
execute_process(
  COMMAND "${ELLC}" compile "${WORK_DIR}/invalid.ell"
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
file(WRITE "${WORK_DIR}/project/ell.json"
     "{\"data\":\"data.json\",\"out\":\"out\"}")
file(WRITE "${WORK_DIR}/project/data.json" "{}")
file(WRITE "${WORK_DIR}/project/plain.ell" "<p>Hello</p>\n")
execute_process(
  COMMAND "${ELLC}" build "${WORK_DIR}/project"
  RESULT_VARIABLE BUILD_RESULT
  ERROR_VARIABLE BUILD_ERROR
)
if(NOT BUILD_RESULT EQUAL 0 OR
   NOT EXISTS "${WORK_DIR}/project/out/plain.html")
  message(FATAL_ERROR "ellc build failed: ${BUILD_ERROR}")
endif()

execute_process(
  COMMAND "${ELLC}" check "${WORK_DIR}/project/plain.ell"
  RESULT_VARIABLE CHECK_RESULT
  ERROR_VARIABLE CHECK_ERROR
)
if(NOT CHECK_RESULT EQUAL 0)
  message(FATAL_ERROR "ellc check failed: ${CHECK_ERROR}")
endif()

file(WRITE "${WORK_DIR}/format.ell" "<p>trailing   \r\n")
execute_process(
  COMMAND "${ELLC}" fmt "${WORK_DIR}/format.ell" --write
  RESULT_VARIABLE FORMAT_RESULT
)
file(READ "${WORK_DIR}/format.ell" FORMATTED)
if(NOT FORMAT_RESULT EQUAL 0 OR NOT FORMATTED STREQUAL "<p>trailing\n")
  message(FATAL_ERROR "ellc fmt --write failed")
endif()

execute_process(
  COMMAND "${ELLC}" schema
  RESULT_VARIABLE SCHEMA_RESULT
  OUTPUT_VARIABLE SCHEMA_OUTPUT
)
if(NOT SCHEMA_RESULT EQUAL 0 OR NOT SCHEMA_OUTPUT MATCHES "ELL project configuration")
  message(FATAL_ERROR "ellc schema failed")
endif()

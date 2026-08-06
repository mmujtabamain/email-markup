file(REMOVE_RECURSE "${INSTALL_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${INSTALL_DIR}"
  RESULT_VARIABLE INSTALL_RESULT
  ERROR_VARIABLE INSTALL_ERROR
)
if(NOT INSTALL_RESULT EQUAL 0)
  message(FATAL_ERROR "install failed: ${INSTALL_ERROR}")
endif()

set(INSTALLED_ELLC "${INSTALL_DIR}/bin/ellc")
if(WIN32)
  set(INSTALLED_ELLC "${INSTALL_DIR}/bin/ellc.exe")
endif()

set(WORK_DIR "${BUILD_DIR}/installed-contract")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/examples/solution_first.ell" DESTINATION "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/examples/solution_first.json" DESTINATION "${WORK_DIR}")
execute_process(
  COMMAND "${INSTALLED_ELLC}" compile "${WORK_DIR}/solution_first.ell"
          --data-file "${WORK_DIR}/solution_first.json"
          -o "${WORK_DIR}/solution_first.html"
  RESULT_VARIABLE COMPILE_RESULT
  ERROR_VARIABLE COMPILE_ERROR
)
if(NOT COMPILE_RESULT EQUAL 0)
  message(FATAL_ERROR "installed ellc failed: ${COMPILE_ERROR}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${WORK_DIR}/solution_first.html"
          "${SOURCE_DIR}/examples/solution_first.html"
  RESULT_VARIABLE COMPARE_RESULT
)
if(NOT COMPARE_RESULT EQUAL 0)
  message(FATAL_ERROR "installed ellc output differs from the golden")
endif()

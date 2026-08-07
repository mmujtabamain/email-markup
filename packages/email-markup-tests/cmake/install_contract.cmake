file(REMOVE_RECURSE "${INSTALL_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${INSTALL_DIR}"
  RESULT_VARIABLE INSTALL_RESULT
  ERROR_VARIABLE INSTALL_ERROR
)
if(NOT INSTALL_RESULT EQUAL 0)
  message(FATAL_ERROR "install failed: ${INSTALL_ERROR}")
endif()

set(INSTALLED_EMC "${INSTALL_DIR}/bin/emc")
if(WIN32)
  set(INSTALLED_EMC "${INSTALL_DIR}/bin/emc.exe")
endif()

set(WORK_DIR "${BUILD_DIR}/installed-contract")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/examples")
file(COPY "${SOURCE_DIR}/examples/01-interpolation" DESTINATION "${WORK_DIR}/examples")
file(COPY "${SOURCE_DIR}/examples/_shared" DESTINATION "${WORK_DIR}/examples")
execute_process(
  COMMAND "${INSTALLED_EMC}" compile "${WORK_DIR}/examples/01-interpolation/message.em"
          --data-file "${WORK_DIR}/examples/01-interpolation/data.json"
          -o "${WORK_DIR}/message.html"
  RESULT_VARIABLE COMPILE_RESULT
  ERROR_VARIABLE COMPILE_ERROR
)
if(NOT COMPILE_RESULT EQUAL 0)
  message(FATAL_ERROR "installed emc failed: ${COMPILE_ERROR}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${WORK_DIR}/message.html"
          "${SOURCE_DIR}/examples/01-interpolation/message.html"
  RESULT_VARIABLE COMPARE_RESULT
)
if(NOT COMPARE_RESULT EQUAL 0)
  message(FATAL_ERROR "installed emc output differs from the golden")
endif()

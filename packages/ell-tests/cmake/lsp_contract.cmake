set(URI "file://${SOURCE_DIR}/examples/lsp_contract.ell")
set(WIRE "")

function(append_message BODY)
  string(LENGTH "${BODY}" BODY_LENGTH)
  set(WIRE "${WIRE}Content-Length: ${BODY_LENGTH}\r\n\r\n${BODY}" PARENT_SCOPE)
endfunction()

append_message("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"workspaceFolders\":[{\"uri\":\"file://${SOURCE_DIR}\",\"name\":\"ell\"}],\"capabilities\":{}}}")
append_message("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}")
append_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\",\"languageId\":\"ell\",\"version\":1,\"text\":\"@DefineComponent(name: \\\"Card\\\")\\n  @Props\\n    title: string\\n  @/Props\\n  @Slots\\n    default: required\\n  @/Slots\\n  @Template\\n    <section>@{title}: @Slot(default);</section>\\n  @/Template\\n@/DefineComponent\\n\\n@Card(title: \\\"Greeting\\\") \\ud83d\\udc4b Hi @/Card\\n@Include(\\\"08-includes/components/notice.ell\\\");\\nordinary prose\"}}}")
append_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\",\"version\":2},\"contentChanges\":[{\"range\":{\"start\":{\"line\":12,\"character\":28},\"end\":{\"line\":12,\"character\":30}},\"text\":\"Hello\"}]}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":12,\"character\":2}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":12,\"character\":3}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":12,\"character\":3}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"textDocument/documentSymbol\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"textDocument/foldingRange\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"textDocument/signatureHelp\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":12,\"character\":12}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"textDocument/formatting\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"options\":{\"tabSize\":2,\"insertSpaces\":true}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"ell/preview\",\"params\":{\"uri\":\"${URI}\"}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":14,\"character\":14}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":12,\"character\":6}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":12,\"character\":13}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":13,\"character\":10}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":16,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":2,\"character\":11}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":17,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":5,\"character\":13}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":18,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":8,\"character\":32}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":19,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":8,\"character\":17}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":12,\"character\":8}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"textDocument/references\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":2,\"character\":6},\"context\":{\"includeDeclaration\":false}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":22,\"method\":\"textDocument/references\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":5,\"character\":7},\"context\":{\"includeDeclaration\":false}}}")
append_message("{\"jsonrpc\":\"2.0\",\"method\":\"$/cancelRequest\",\"params\":{\"id\":11}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"},\"position\":{\"line\":12,\"character\":2}}}")
append_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":\"${URI}\"}}}")
append_message("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"shutdown\",\"params\":null}")
append_message("{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}")

set(INPUT "${BINARY_DIR}/lsp-contract.input")
file(WRITE "${INPUT}" "${WIRE}")
execute_process(
  COMMAND "${ELL_LSP}"
  INPUT_FILE "${INPUT}"
  OUTPUT_VARIABLE OUTPUT
  ERROR_VARIABLE ERROR_OUTPUT
  RESULT_VARIABLE RESULT
)
if(NOT RESULT EQUAL 0)
  message(FATAL_ERROR "ell-lsp failed (${RESULT}): ${ERROR_OUTPUT}")
endif()
foreach(REQUIRED
    "\\\"positionEncoding\\\":\\\"utf-16\\\""
    "textDocument/publishDiagnostics"
    "\\\"id\\\":2"
    "@Paragraph"
    "token.accent"
    "Compile data"
    "builtins.ell"
    "ELL prop: string"
    "\\\"id\\\":3"
    "\\\"id\\\":4"
    "\\\"id\\\":5"
    "\\\"id\\\":6"
    "\\\"id\\\":7"
    "\\\"id\\\":8"
    "\\\"id\\\":9"
    "\\\"id\\\":12"
    "\\\"id\\\":13"
    "\\\"id\\\":14"
    "\\\"id\\\":15"
    "\\\"id\\\":16"
    "ELL prop type"
    "\\\"id\\\":17"
    "ELL slot requirement"
    "\\\"id\\\":18"
    "\\\"id\\\":19"
    "\\\"id\\\":20"
    "\\\"referencesProvider\\\":true"
    "\\\"id\\\":21"
    "\\\"id\\\":22"
    "\\\"html\\\":\\\""
    "\\\"version\\\":2"
    "-32800"
    "\\\"id\\\":10")
  if(NOT OUTPUT MATCHES "${REQUIRED}")
    message(FATAL_ERROR "ell-lsp output omitted ${REQUIRED}: ${OUTPUT}")
  endif()
endforeach()
if(NOT OUTPUT MATCHES "\\\"id\\\":12,\\\"jsonrpc\\\":\\\"2.0\\\",\\\"result\\\":\\{\\\"isIncomplete\\\":false,\\\"items\\\":\\[\\]\\}")
  message(FATAL_ERROR "ell-lsp returned completions for ordinary prose: ${OUTPUT}")
endif()
foreach(REFERENCE_ID 21 22)
  if(OUTPUT MATCHES "\\\"id\\\":${REFERENCE_ID},\\\"jsonrpc\\\":\\\"2.0\\\",\\\"result\\\":\\[\\]")
    message(FATAL_ERROR "ell-lsp returned no references for request ${REFERENCE_ID}: ${OUTPUT}")
  endif()
endforeach()

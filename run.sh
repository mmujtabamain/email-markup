#!/bin/bash
set -euo pipefail

if [ -t 1 ]; then
  clear
  clear
  clear
fi

RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
YELLOW=$'\033[0;33m'
BLUE=$'\033[0;34m'
GRAY=$'\033[0;90m'
BOLD=$'\033[1m'
RESET=$'\033[0m'

log()     { printf '%s\n' "${BLUE}> $* ${RESET}"; }
step()    { printf '\n%s\n' "${BOLD}${BLUE}==> ${BOLD}$*${RESET}"; }
success() { printf '%s\n' "${GREEN}$* ${RESET}"; }
warn()    { printf '%s\n' "${YELLOW}$* ${RESET}"; }
fail()    { printf '%s\n' "${RED}$* ${RESET}" >&2; }

trap 'fail "Failed at line $LINENO. Aborting — nothing further will run."' ERR

run() {
  log "${GRAY}\$ $*${RESET}"
  "$@"
}

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
COMMAND=${1:-help}
BUILD_MODE=${2:-debug}

case "$BUILD_MODE" in
  debug|release) ;;
  --release) BUILD_MODE=release ;;
  *)
    fail "Unknown build mode: $BUILD_MODE"
    exit 2
    ;;
esac

cd "$REPO_DIR"

configure() {
  run cmake --preset "$BUILD_MODE"
}

build() {
  configure
  run cmake --build --preset "$BUILD_MODE"
}

case "$COMMAND" in
  build)
    step "Step 1/1 — Build ELL ($BUILD_MODE)"
    build
    success "Built ellc, ell-lsp, and ell-tests."
    ;;
  test)
    step "Step 1/2 — Build ELL ($BUILD_MODE)"
    build
    step "Step 2/2 — Run tests"
    run ctest --preset "$BUILD_MODE"
    success "All ELL tests passed."
    ;;
  ellc)
    step "Step 1/2 — Build ELL ($BUILD_MODE)"
    build
    step "Step 2/2 — Run ellc"
    run "$REPO_DIR/build/$BUILD_MODE/bin/ellc"
    ;;
  ell-lsp)
    step "Step 1/2 — Build ELL ($BUILD_MODE)"
    build
    step "Step 2/2 — Run ell-lsp"
    run "$REPO_DIR/build/$BUILD_MODE/bin/ell-lsp"
    ;;
  help|-h|--help)
    printf '%s\n' \
      "Usage: ./run.sh <command> [debug|release]" \
      "" \
      "Commands:" \
      "  build      Configure and build all targets" \
      "  test       Build and run the test suite" \
      "  ellc       Build and run the compiler" \
      "  ell-lsp    Build and run the language server" \
      "  help       Show this help"
    ;;
  *)
    fail "Unknown command: $COMMAND"
    exit 2
    ;;
esac

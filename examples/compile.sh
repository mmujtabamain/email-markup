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

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_MODE=${1:-debug}

log()     { printf '%s\n' "${BLUE}> $* ${RESET}"; }
step()    { printf '\n%s\n' "${BOLD}${BLUE}==> ${BOLD}$*${RESET}"; }
success() { printf '%s\n' "${GREEN}$* ${RESET}"; }
warn()    { printf '%s\n' "${YELLOW}$* ${RESET}"; }
fail()    { printf '%s\n' "${RED}$* ${RESET}" >&2; }

trap 'fail "Failed at line $LINENO. Aborting - nothing further will run."' ERR

run() {
  log "${GRAY}\$ $*${RESET}"
  "$@"
}

case "$BUILD_MODE" in
  debug|release) ;;
  *)
    fail "Build mode must be debug or release."
    exit 2
    ;;
esac

if [ -n "${ELLC:-}" ]; then
  step "Step 1/2 - Use the configured ELL compiler"
  run "$ELLC" --version
else
  step "Step 1/2 - Build the ELL compiler ($BUILD_MODE)"
  run "$REPO_DIR/run.sh" build "$BUILD_MODE"
  ELLC="$REPO_DIR/build/$BUILD_MODE/bin/ellc"
fi

step "Step 2/2 - Compile all ten examples"
for example in \
  01-interpolation \
  02-conditionals \
  03-loops \
  04-expressions \
  05-typed-props \
  06-named-slots \
  07-tokens \
  08-includes \
  09-css-inlining \
  10-responsive-media
do
  run "$ELLC" compile "$SCRIPT_DIR/$example/message.ell" \
    -o "$SCRIPT_DIR/$example/message.html"
done

success "Compiled all ten examples."

#!/bin/bash

SCRIPT_PATH=${BASH_SOURCE[0]:-$0}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$SCRIPT_PATH")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
EMSCRIPTEN_VERSION=4.0.23
EMSDK_COMMIT=c0bb220cb6e6f4e0fabb6f6db9efd53390ef5e56
EMSDK_PARENT_DIR="$REPO_DIR/.cache"
EMSDK_DIR="$EMSDK_PARENT_DIR/emsdk"
EMSDK_STATE_FILE="$EMSDK_DIR/.email-markup-version"
EMSCRIPTEN_STATE_FILE="$EMSDK_DIR/.email-markup-emscripten-version"

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

run() {
  log "${GRAY}\$ $*${RESET}"
  "$@"
}

has_required_emscripten() {
  local version_output

  command -v emcc >/dev/null 2>&1 || return 1
  command -v emcmake >/dev/null 2>&1 || return 1
  version_output=$(emcc --version 2>/dev/null) || return 1

  case "$version_output" in
    *" $EMSCRIPTEN_VERSION "*) return 0 ;;
    *) return 1 ;;
  esac
}

prepare_emsdk() (
  set -euo pipefail
  trap 'fail "Failed at line $LINENO. Aborting — nothing further will run."' ERR

  step "Step 1/3 — Check the pinned emsdk checkout"
  if [ -f "$EMSDK_DIR/emsdk" ] && [ -f "$EMSDK_STATE_FILE" ] &&
     [ "$(sed -n '1p' "$EMSDK_STATE_FILE")" = "$EMSDK_COMMIT" ]; then
    success "emsdk is already downloaded."
  else
    warn "Emscripten $EMSCRIPTEN_VERSION is not available. Downloading emsdk."

    if ! command -v git >/dev/null 2>&1; then
      fail "Missing required command: git"
      exit 1
    fi

    run mkdir -p "$EMSDK_PARENT_DIR"
    if [ -e "$EMSDK_DIR" ]; then
      warn "Replacing the incomplete or outdated cached emsdk checkout."
      run rm -rf "$EMSDK_DIR"
    fi

    run git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
    run git -C "$EMSDK_DIR" checkout --detach "$EMSDK_COMMIT"
    printf '%s\n' "$EMSDK_COMMIT" > "$EMSDK_STATE_FILE"
    run rm -rf "$EMSDK_DIR/.git"
  fi

  step "Step 2/3 — Install Emscripten $EMSCRIPTEN_VERSION"
  if [ -x "$EMSDK_DIR/upstream/emscripten/emcc" ] &&
     [ -f "$EMSCRIPTEN_STATE_FILE" ] &&
     [ "$(sed -n '1p' "$EMSCRIPTEN_STATE_FILE")" = "$EMSCRIPTEN_VERSION" ]; then
    success "Emscripten $EMSCRIPTEN_VERSION is already installed."
  else
    run "$EMSDK_DIR/emsdk" install "$EMSCRIPTEN_VERSION"
    printf '%s\n' "$EMSCRIPTEN_VERSION" > "$EMSCRIPTEN_STATE_FILE"
  fi

  step "Step 3/3 — Activate Emscripten $EMSCRIPTEN_VERSION"
  run "$EMSDK_DIR/emsdk" activate "$EMSCRIPTEN_VERSION"
)

if has_required_emscripten; then
  success "Emscripten $EMSCRIPTEN_VERSION is already available."
else
  return_code=0
  prepare_emsdk || return_code=$?
  if [ "$return_code" -ne 0 ]; then
    return "$return_code" 2>/dev/null || exit "$return_code"
  fi

  # shellcheck disable=SC1091
  if ! source "$EMSDK_DIR/emsdk_env.sh"; then
    fail "Could not load the emsdk environment."
    return 1 2>/dev/null || exit 1
  fi

  if ! has_required_emscripten; then
    fail "emsdk did not provide Emscripten $EMSCRIPTEN_VERSION."
    return 1 2>/dev/null || exit 1
  fi

  success "Emscripten $EMSCRIPTEN_VERSION is installed, activated, and loaded."
  if [ "$SCRIPT_PATH" = "$0" ]; then
    warn "Run 'source $SCRIPT_PATH' to load Emscripten into your current shell."
  fi
fi

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
VCPKG_PARENT_DIR="$REPO_DIR/external"
VCPKG_DIR="$REPO_DIR/external/vcpkg"
VCPKG_VERSION_FILE="$REPO_DIR/external/vcpkg.version"
VCPKG_STATE_FILE="$VCPKG_DIR/.email-markup-version"
TOTAL_STEPS=4

cd "$REPO_DIR"

step "Step 1/$TOTAL_STEPS — Check build prerequisites"
for command_name in git cmake ninja pkg-config; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    fail "Missing required command: $command_name"
    exit 1
  fi
done

step "Step 2/$TOTAL_STEPS — Clone vcpkg"
run mkdir -p "$VCPKG_PARENT_DIR"
VCPKG_VERSION=""
if [ -f "$VCPKG_VERSION_FILE" ]; then
  VCPKG_VERSION=$(sed -n '1{s/^[[:space:]]*//;s/[[:space:]]*$//;p;}' "$VCPKG_VERSION_FILE")
fi
VCPKG_REQUESTED_VERSION=${VCPKG_VERSION:-latest}

if [ -f "$VCPKG_DIR/bootstrap-vcpkg.sh" ]; then
  VCPKG_INSTALLED_VERSION=""
  if [ -f "$VCPKG_STATE_FILE" ]; then
    VCPKG_INSTALLED_VERSION=$(sed -n '1p' "$VCPKG_STATE_FILE")
  fi

  if [ "$VCPKG_INSTALLED_VERSION" = "$VCPKG_REQUESTED_VERSION" ]; then
    success "vcpkg $VCPKG_REQUESTED_VERSION is already cloned."
  else
    warn "The requested vcpkg version changed; replacing the generated checkout."
    run rm -rf "$VCPKG_DIR"
  fi
elif [ -e "$VCPKG_DIR" ]; then
  fail "$VCPKG_DIR exists but is not a valid vcpkg checkout."
  exit 1
fi

if [ ! -f "$VCPKG_DIR/bootstrap-vcpkg.sh" ]; then
  if [ -n "$VCPKG_VERSION" ]; then
    run mkdir "$VCPKG_DIR"
    run git -C "$VCPKG_DIR" init
    run git -C "$VCPKG_DIR" remote add origin https://github.com/microsoft/vcpkg.git
    run git -C "$VCPKG_DIR" fetch --depth 1 origin "$VCPKG_VERSION"
    run git -C "$VCPKG_DIR" checkout --detach FETCH_HEAD
  else
    run git clone --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
    VCPKG_VERSION=$(git -C "$VCPKG_DIR" rev-parse --verify HEAD)
    VCPKG_REQUESTED_VERSION=$VCPKG_VERSION
    printf '%s\n' "$VCPKG_VERSION" > "$VCPKG_VERSION_FILE"
    success "Pinned vcpkg to $VCPKG_VERSION."
  fi
  printf '%s\n' "$VCPKG_REQUESTED_VERSION" > "$VCPKG_STATE_FILE"
  run rm -rf "$VCPKG_DIR/.git"
fi

step "Step 3/$TOTAL_STEPS — Bootstrap vcpkg"
if [ -x "$VCPKG_DIR/vcpkg" ]; then
  success "vcpkg is already bootstrapped."
elif [ -f "$VCPKG_DIR/bootstrap-vcpkg.sh" ]; then
  run "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
else
  fail "vcpkg bootstrap script was not found."
  exit 1
fi

step "Step 4/$TOTAL_STEPS — Configure the Debug build"
run cmake --preset debug

success "Setup complete. Run ./run.sh build to compile Email Markup."

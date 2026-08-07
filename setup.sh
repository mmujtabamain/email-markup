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
VCPKG_DIR="$REPO_DIR/external/vcpkg"
TOTAL_STEPS=4

cd "$REPO_DIR"

step "Step 1/$TOTAL_STEPS — Check build prerequisites"
for command_name in git cmake ninja; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    fail "Missing required command: $command_name"
    exit 1
  fi
done

step "Step 2/$TOTAL_STEPS — Initialise the vcpkg submodule"
run git submodule update --init --recursive --depth 1

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

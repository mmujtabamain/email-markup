#include <cstdlib>

#include <fmt/core.h>

#include "ell/core/version.hpp"

int main() {
    fmt::print(stderr, "ELL language server scaffold {}\n", ell::version());
    return EXIT_SUCCESS;
}

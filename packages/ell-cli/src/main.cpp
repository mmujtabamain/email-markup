#include <cstdlib>
#include <string_view>

#include <fmt/core.h>

#include "ell/core/version.hpp"

int main(const int argc, const char* const argv[]) {
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        fmt::print("ellc {}\n", ell::version());
        return EXIT_SUCCESS;
    }

    fmt::print("ELL compiler scaffold {}\n", ell::version());
    return EXIT_SUCCESS;
}

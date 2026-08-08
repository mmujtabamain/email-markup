#include "app/application.hpp"

int main(const int argc, const char *const argv[])
{
    return email_markup::cli::Application{argc, argv}.run();
}

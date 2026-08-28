#include "server/server.hpp"

int main(const int argc, const char *const argv[])
{
    return email_markup::lsp::Server{email_markup::platform::System{
                                         argc > 0 ? argv[0] : "email-markup-lsp"}}
        .run();
}

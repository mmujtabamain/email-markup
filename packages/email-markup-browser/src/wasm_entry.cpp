#include "email-markup/browser/protocol.hpp"

#include <cstring>
#include <string>

#include <emscripten/emscripten.h>

extern "C"
{
    EMSCRIPTEN_KEEPALIVE int email_markup_browser_protocol_version()
    {
        return email_markup::browser::protocol_version;
    }

    EMSCRIPTEN_KEEPALIVE const char *email_markup_browser_request(const char *request)
    {
        static thread_local std::string response;
        if (!request)
            response = email_markup::browser::handle_request({});
        else
            response = email_markup::browser::handle_request(
                {request, std::strlen(request)});
        return response.c_str();
    }
}

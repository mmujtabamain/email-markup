#include "email-markup/browser/protocol.hpp"

#include <cstring>
#include <exception>
#include <string>

#include <emscripten/emscripten.h>

namespace
{
    /**
     * A JSON error envelope built without the JSON library, for the one case
     * where the library itself may be what failed.
     */
    std::string bare_error(const char *message)
    {
        std::string escaped;
        for (const char *cursor = message; cursor && *cursor; ++cursor)
        {
            if (*cursor == '"' || *cursor == '\\')
                escaped.push_back('\\');
            if (static_cast<unsigned char>(*cursor) < 0x20)
                continue;
            escaped.push_back(*cursor);
        }
        return std::string{"{\"protocol\":\"email-markup.browser\",\"version\":1,"
                           "\"compiler_version\":null,\"id\":null,\"ok\":false,"
                           "\"error\":{\"code\":\"internal_error\",\"message\":\""} +
               escaped + "\"}}\n";
    }
}

extern "C"
{
    EMSCRIPTEN_KEEPALIVE int email_markup_browser_protocol_version()
    {
        return email_markup::browser::protocol_version;
    }

    /**
     * `handle_request` already answers every failure it can see with a JSON error.
     * This second net is for the failures it cannot: an allocation failure while
     * the response is being copied out, or anything thrown on the way back. An
     * exception that escapes here reaches JavaScript as a bare pointer, which the
     * editor can only report as a number — a stack trace made of one integer.
     */
    EMSCRIPTEN_KEEPALIVE const char *email_markup_browser_request(const char *request)
    {
        static thread_local std::string response;
        try
        {
            if (!request)
                response = email_markup::browser::handle_request({});
            else
                response = email_markup::browser::handle_request(
                    {request, std::strlen(request)});
        }
        catch (const std::exception &error)
        {
            try
            {
                response = bare_error(error.what());
            }
            catch (...)
            {
                return "{\"protocol\":\"email-markup.browser\",\"version\":1,"
                       "\"compiler_version\":null,\"id\":null,\"ok\":false,\"error\":"
                       "{\"code\":\"internal_error\",\"message\":\"out of memory\"}}\n";
            }
        }
        catch (...)
        {
            try
            {
                response = bare_error(
                    "the browser compiler failed in a way it could not describe");
            }
            catch (...)
            {
                return "{\"protocol\":\"email-markup.browser\",\"version\":1,"
                       "\"compiler_version\":null,\"id\":null,\"ok\":false,\"error\":"
                       "{\"code\":\"internal_error\",\"message\":\"out of memory\"}}\n";
            }
        }
        return response.c_str();
    }
}

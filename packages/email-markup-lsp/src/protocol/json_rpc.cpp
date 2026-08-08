#include "json_rpc.hpp"

#include <istream>
#include <ostream>
#include <string_view>
#include <utility>

namespace email_markup::lsp
{
    JsonRpcConnection::JsonRpcConnection(std::istream &input, std::ostream &output) noexcept
        : input_(input), output_(output)
    {
    }

    std::optional<Json> JsonRpcConnection::read()
    {
        std::string line;
        std::size_t length = 0;
        while (std::getline(input_, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                break;
            constexpr std::string_view header{"Content-Length:"};
            if (line.starts_with(header))
                length = static_cast<std::size_t>(std::stoull(line.substr(header.size())));
        }
        if (!input_ || length == 0)
            return std::nullopt;

        std::string body(length, '\0');
        input_.read(body.data(), static_cast<std::streamsize>(length));
        if (static_cast<std::size_t>(input_.gcount()) != length)
            return std::nullopt;
        try
        {
            return Json::parse(body);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void JsonRpcConnection::send(const Json &message)
    {
        const auto body = message.dump();
        output_ << "Content-Length: " << body.size() << "\r\n\r\n"
                << body;
        output_.flush();
    }

    void JsonRpcConnection::respond(const Json &id, Json result)
    {
        send({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
    }

    void JsonRpcConnection::error(const Json &id, const int code, std::string message)
    {
        send({{"jsonrpc", "2.0"},
              {"id", id},
              {"error", {{"code", code}, {"message", std::move(message)}}}});
    }

    void JsonRpcConnection::notify(std::string method, Json params)
    {
        send({{"jsonrpc", "2.0"},
              {"method", std::move(method)},
              {"params", std::move(params)}});
    }
} // namespace email_markup::lsp

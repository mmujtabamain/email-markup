#pragma once

#include <iosfwd>
#include <optional>
#include <string>

#include "protocol/json.hpp"

namespace email_markup::lsp
{
    class JsonRpcConnection final
    {
    public:
        JsonRpcConnection(std::istream &input, std::ostream &output) noexcept;

        JsonRpcConnection(const JsonRpcConnection &) = delete;
        JsonRpcConnection &operator=(const JsonRpcConnection &) = delete;

        [[nodiscard]] std::optional<Json> read();
        void respond(const Json &id, Json result);
        void error(const Json &id, int code, std::string message);
        void notify(std::string method, Json params);

    private:
        void send(const Json &message);

        std::istream &input_;
        std::ostream &output_;
    };
} // namespace email_markup::lsp

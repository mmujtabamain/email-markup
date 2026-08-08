#pragma once

#include <iosfwd>
#include <string>
#include <unordered_set>

#include "documents/document_store.hpp"
#include "email-markup/platform/system.hpp"
#include "email-markup/runtime/assets.hpp"
#include "protocol/json_rpc.hpp"
#include "workspace/workspace.hpp"

namespace email_markup::lsp
{
    class Server final
    {
    public:
        explicit Server(platform::System system);
        Server(platform::System system, std::istream &input, std::ostream &output);

        Server(const Server &) = delete;
        Server &operator=(const Server &) = delete;

        [[nodiscard]] int run();

    private:
        void respond(const Json &id, Json result);
        void error(const Json &id, int code, std::string message);
        void notify(std::string method, Json params);

        void handle(const Json &message);
        void initialize(const Json &id, const Json &params);
        [[nodiscard]] OpenDocument *document(const Json &params);
        void did_open(const Json &params);
        void did_change(const Json &params);
        void did_close(const Json &params);
        void publish(const std::string &uri, const OpenDocument &document);
        void preview(const Json &id, const Json &params);

        void completion(const Json &id, const Json &params);
        void hover(const Json &id, const Json &params);
        void definition(const Json &id, const Json &params);
        void references(const Json &id, const Json &params);
        void symbols(const Json &id, const Json &params);
        void folding(const Json &id, const Json &params);
        void signature(const Json &id, const Json &params);
        void formatting(const Json &id, const Json &params);

        platform::System system_;
        runtime::Assets assets_;
        JsonRpcConnection connection_;
        Workspace workspace_;
        DocumentStore documents_;
        std::unordered_set<std::string> cancelled_;
        bool shutdown_requested_{};
        bool exiting_{};
    };
} // namespace email_markup::lsp

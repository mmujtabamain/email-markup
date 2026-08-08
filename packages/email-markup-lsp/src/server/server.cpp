#include "server.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include "email-markup/core/version.hpp"

namespace email_markup::lsp
{
    Server::Server(platform::System system)
        : Server(std::move(system), std::cin, std::cout)
    {
    }

    Server::Server(platform::System system, std::istream &input, std::ostream &output)
        : system_(std::move(system)), assets_(system_), connection_(input, output),
          workspace_(system_, assets_)
    {
    }

    int Server::run()
    {
        while (auto message = connection_.read())
        {
            handle(*message);
            if (exiting_)
                break;
        }
        return shutdown_requested_ ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    void Server::respond(const Json &id, Json result)
    {
        connection_.respond(id, std::move(result));
    }

    void Server::error(const Json &id, const int code, std::string message)
    {
        connection_.error(id, code, std::move(message));
    }

    void Server::notify(std::string method, Json params)
    {
        connection_.notify(std::move(method), std::move(params));
    }

    void Server::handle(const Json &message)
    {
        const auto method = message.value("method", "");
        const auto params = message.value("params", Json::object());
        const auto has_id = message.contains("id");
        const auto id = has_id ? message.at("id") : Json{};

        if (method == "$/cancelRequest")
        {
            cancelled_.insert(params.value("id", Json{}).dump());
            return;
        }
        if (has_id && cancelled_.erase(id.dump()) > 0)
        {
            error(id, -32800, "Request cancelled");
            return;
        }
        if (method == "initialize")
        {
            initialize(id, params);
            return;
        }
        if (method == "initialized")
            return;
        if (method == "shutdown")
        {
            shutdown_requested_ = true;
            respond(id, nullptr);
            return;
        }
        if (method == "exit")
        {
            exiting_ = true;
            return;
        }
        if (method == "textDocument/didOpen")
        {
            did_open(params);
            return;
        }
        if (method == "textDocument/didChange")
        {
            did_change(params);
            return;
        }
        if (method == "textDocument/didClose")
        {
            did_close(params);
            return;
        }
        if (method == "workspace/didChangeWatchedFiles")
        {
            for (const auto &[uri, document] : documents_.all())
                publish(uri, document);
            return;
        }
        if (method == "textDocument/completion")
        {
            completion(id, params);
            return;
        }
        if (method == "textDocument/hover")
        {
            hover(id, params);
            return;
        }
        if (method == "textDocument/definition")
        {
            definition(id, params);
            return;
        }
        if (method == "textDocument/references")
        {
            references(id, params);
            return;
        }
        if (method == "textDocument/documentSymbol")
        {
            symbols(id, params);
            return;
        }
        if (method == "textDocument/foldingRange")
        {
            folding(id, params);
            return;
        }
        if (method == "textDocument/signatureHelp")
        {
            signature(id, params);
            return;
        }
        if (method == "textDocument/formatting")
        {
            formatting(id, params);
            return;
        }
        if (method == "email-markup/protocolVersion")
        {
            respond(id, {{"version", 1}});
            return;
        }
        if (method == "email-markup/preview")
        {
            preview(id, params);
            return;
        }
        if (has_id)
            error(id, -32601, "Method not found: " + method);
    }

    void Server::initialize(const Json &id, const Json &params)
    {
        std::vector<std::filesystem::path> roots;
        for (const auto &folder : params.value("workspaceFolders", Json::array()))
            roots.push_back(system_.path_from_file_uri(folder.value("uri", "")));
        workspace_.set_roots(std::move(roots));

        respond(id,
                {{"serverInfo",
                  {{"name", "email-markup-lsp"}, {"version", email_markup::version()}}},
                 {"capabilities",
                  {{"positionEncoding", "utf-16"},
                   {"textDocumentSync", {{"openClose", true}, {"change", 2}}},
                   {"completionProvider",
                    {{"triggerCharacters",
                      Json::array({"@", "/", "(", ",", ":", "=", "{", "\""})}}},
                   {"hoverProvider", true},
                   {"definitionProvider", true},
                   {"referencesProvider", true},
                   {"documentSymbolProvider", true},
                   {"foldingRangeProvider", true},
                   {"signatureHelpProvider",
                    {{"triggerCharacters", Json::array({"(", ","})}}},
                   {"documentFormattingProvider", true}}}});
    }

    OpenDocument *Server::document(const Json &params)
    {
        return documents_.find(params.at("textDocument").value("uri", ""));
    }

    void Server::did_open(const Json &params)
    {
        const auto &item = params.at("textDocument");
        const auto uri = item.value("uri", "");
        auto &open = documents_.open(uri, system_.path_from_file_uri(uri),
                                     item.value("text", ""), item.value("version", 0));
        publish(uri, open);
    }

    void Server::did_change(const Json &params)
    {
        const auto uri = params.at("textDocument").value("uri", "");
        if (auto *open = documents_.apply_changes(params))
            publish(uri, *open);
    }

    void Server::did_close(const Json &params)
    {
        const auto uri = params.at("textDocument").value("uri", "");
        documents_.close(uri);
        notify("textDocument/publishDiagnostics",
               {{"uri", uri}, {"diagnostics", Json::array()}});
    }

    void Server::publish(const std::string &uri, const OpenDocument &document)
    {
        const auto version = document.version;
        const auto result = workspace_.compile(document);
        if (!documents_.has_version(uri, version))
            return;
        notify("textDocument/publishDiagnostics",
               {{"uri", uri},
                {"version", version},
                {"diagnostics", workspace_.diagnostics(result, document)}});
    }

    void Server::preview(const Json &id, const Json &params)
    {
        const auto uri = params.value("uri", "");
        const auto *document = documents_.find(uri);
        if (!document)
        {
            error(id, -32602, "Document is not open");
            return;
        }
        const Json *data = params.contains("data") ? &params.at("data") : nullptr;
        const auto version = document->version;
        auto result = workspace_.compile(*document, data);
        if (!documents_.has_version(uri, version))
        {
            error(id, -32801, "Preview became stale");
            return;
        }
        respond(id, {{"version", version},
                     {"html", result.ok() ? Json(result.generated.html) : Json{}},
                     {"diagnostics", workspace_.diagnostics(result, *document)}});
    }
} // namespace email_markup::lsp

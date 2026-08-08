#include "document_store.hpp"

#include <utility>

#include "text/positions.hpp"

namespace email_markup::lsp
{
    OpenDocument &DocumentStore::open(std::string uri, std::filesystem::path path,
                                      std::string text, const std::int64_t version)
    {
        const auto result = documents_.insert_or_assign(
            std::move(uri), OpenDocument{std::move(path), std::move(text), version});
        return result.first->second;
    }

    OpenDocument *DocumentStore::find(const std::string &uri) noexcept
    {
        const auto found = documents_.find(uri);
        return found == documents_.end() ? nullptr : &found->second;
    }

    const OpenDocument *DocumentStore::find(const std::string &uri) const noexcept
    {
        const auto found = documents_.find(uri);
        return found == documents_.end() ? nullptr : &found->second;
    }

    OpenDocument *DocumentStore::apply_changes(const Json &params)
    {
        const auto uri = params.at("textDocument").value("uri", "");
        auto *document = find(uri);
        if (!document)
            return nullptr;

        const auto incoming_version =
            params.at("textDocument").value("version", document->version + 1);
        for (const auto &change : params.value("contentChanges", Json::array()))
        {
            if (!change.contains("range"))
            {
                document->text = change.value("text", "");
                continue;
            }
            const auto &range = change.at("range");
            const auto start = text::offset_at(
                document->text, range.at("start").value("line", 0),
                range.at("start").value("character", 0));
            const auto end = text::offset_at(
                document->text, range.at("end").value("line", 0),
                range.at("end").value("character", 0));
            document->text.replace(start, end - start, change.value("text", ""));
        }
        document->version = incoming_version;
        return document;
    }

    void DocumentStore::close(const std::string &uri)
    {
        documents_.erase(uri);
    }

    bool DocumentStore::has_version(const std::string &uri,
                                    const std::int64_t version) const noexcept
    {
        const auto *document = find(uri);
        return document && document->version == version;
    }

    const std::unordered_map<std::string, OpenDocument> &DocumentStore::all() const noexcept
    {
        return documents_;
    }
} // namespace email_markup::lsp

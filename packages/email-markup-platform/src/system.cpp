#include "email-markup/platform/system.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "native.hpp"

namespace email_markup::platform
{
    namespace
    {
        std::filesystem::path canonical_path(const std::filesystem::path &path)
        {
            std::error_code error;
            const auto absolute = std::filesystem::absolute(path, error);
            if (error)
                return path;
            const auto canonical = std::filesystem::weakly_canonical(absolute, error);
            return error ? absolute : canonical;
        }

        int hex_value(const char value)
        {
            if (value >= '0' && value <= '9')
                return value - '0';
            if (value >= 'a' && value <= 'f')
                return value - 'a' + 10;
            if (value >= 'A' && value <= 'F')
                return value - 'A' + 10;
            return -1;
        }

        std::string decode_file_uri(std::string_view uri)
        {
            constexpr std::string_view prefix{"file://"};
            if (uri.starts_with(prefix))
                uri.remove_prefix(prefix.size());

            std::string decoded;
            decoded.reserve(uri.size());
            for (std::size_t index = 0; index < uri.size(); ++index)
            {
                if (uri[index] == '%' && index + 2 < uri.size())
                {
                    const auto high = hex_value(uri[index + 1]);
                    const auto low = hex_value(uri[index + 2]);
                    if (high >= 0 && low >= 0)
                    {
                        decoded.push_back(static_cast<char>((high << 4) | low));
                        index += 2;
                        continue;
                    }
                }
                decoded.push_back(uri[index]);
            }
            return decoded;
        }

        class AtomicTextFile final
        {
        public:
            explicit AtomicTextFile(std::filesystem::path destination)
                : destination_(std::move(destination)), temporary_(temporary_path(destination_))
            {
                if (!destination_.parent_path().empty())
                    std::filesystem::create_directories(destination_.parent_path());
                stream_.open(temporary_, std::ios::binary | std::ios::trunc);
                if (!stream_)
                    throw std::runtime_error("Cannot create " + temporary_.string());
            }

            AtomicTextFile(const AtomicTextFile &) = delete;
            AtomicTextFile &operator=(const AtomicTextFile &) = delete;

            ~AtomicTextFile()
            {
                if (!committed_)
                {
                    std::error_code ignored;
                    std::filesystem::remove(temporary_, ignored);
                }
            }

            void commit(const std::string_view contents)
            {
                stream_.write(contents.data(), static_cast<std::streamsize>(contents.size()));
                stream_.flush();
                if (!stream_)
                    throw std::runtime_error("Cannot write " + temporary_.string());
                stream_.close();
                if (!stream_)
                    throw std::runtime_error("Cannot close " + temporary_.string());
                detail::replace_file(temporary_, destination_);
                committed_ = true;
            }

        private:
            static std::filesystem::path temporary_path(const std::filesystem::path &destination)
            {
                static std::atomic_uint64_t sequence{};
                const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
                auto temporary = destination;
                temporary += ".tmp." + std::to_string(timestamp) + "." +
                             std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
                return temporary;
            }

            std::filesystem::path destination_;
            std::filesystem::path temporary_;
            std::ofstream stream_;
            bool committed_{};
        };
    } // namespace

    System::System(const std::string_view invocation)
        : executable_path_(canonical_path(detail::native_executable_path(invocation)))
    {
    }

    const std::filesystem::path &System::executable_path() const noexcept
    {
        return executable_path_;
    }

    std::filesystem::path System::path_from_file_uri(const std::string_view uri) const
    {
        auto decoded = decode_file_uri(uri);
        detail::normalize_file_uri_path(decoded);
        return decoded;
    }

    std::string System::read_text_file(const std::filesystem::path &path) const
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Cannot read " + path.string());
        return {std::istreambuf_iterator<char>{stream}, {}};
    }

    std::string System::read_standard_input() const
    {
        return {std::istreambuf_iterator<char>{std::cin}, {}};
    }

    void System::write_text_file_atomically(const std::filesystem::path &path,
                                            const std::string_view contents) const
    {
        AtomicTextFile{path}.commit(contents);
    }
} // namespace email_markup::platform

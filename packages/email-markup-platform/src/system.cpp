#include "email-markup/platform/system.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <curl/curl.h>

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

        bool private_ipv4(const std::uint32_t ip)
        {
            const auto first = (ip >> 24) & 0xff;
            const auto second = (ip >> 16) & 0xff;
            return first == 0 || first == 10 || first == 127 || first >= 224 ||
                   (first == 100 && second >= 64 && second <= 127) ||
                   (first == 169 && second == 254) ||
                   (first == 172 && second >= 16 && second <= 31) ||
                   (first == 192 && second == 168);
        }

        bool private_address(const curl_sockaddr *address)
        {
            if (address->family == AF_INET)
            {
                const auto *value = reinterpret_cast<const sockaddr_in *>(&address->addr);
                return private_ipv4(ntohl(value->sin_addr.s_addr));
            }
            if (address->family == AF_INET6)
            {
                const auto *value = reinterpret_cast<const sockaddr_in6 *>(&address->addr);
                const auto *bytes = value->sin6_addr.s6_addr;
                const bool loopback = std::all_of(bytes, bytes + 15,
                                                  [](const unsigned char byte)
                                                  { return byte == 0; }) &&
                                      bytes[15] == 1;
                const bool unspecified = std::all_of(bytes, bytes + 16,
                                                     [](const unsigned char byte)
                                                     { return byte == 0; });
                const bool mapped = std::all_of(bytes, bytes + 10,
                                                [](const unsigned char byte)
                                                { return byte == 0; }) &&
                                    bytes[10] == 0xff && bytes[11] == 0xff;
                const auto mapped_ip = (static_cast<std::uint32_t>(bytes[12]) << 24) |
                                       (static_cast<std::uint32_t>(bytes[13]) << 16) |
                                       (static_cast<std::uint32_t>(bytes[14]) << 8) |
                                       static_cast<std::uint32_t>(bytes[15]);
                return loopback || unspecified || (bytes[0] & 0xfe) == 0xfc ||
                       (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) ||
                       bytes[0] == 0xff || (mapped && private_ipv4(mapped_ip));
            }
            return true;
        }

        curl_socket_t open_public_socket(void *, const curlsocktype purpose,
                                         curl_sockaddr *address)
        {
            if (purpose != CURLSOCKTYPE_IPCXN || private_address(address))
                return CURL_SOCKET_BAD;
            return ::socket(address->family, address->socktype, address->protocol);
        }

        struct Download
        {
            std::string bytes;
            std::size_t maximum{};
            bool exceeded{};
        };

        std::size_t receive(void *contents, const std::size_t size,
                            const std::size_t count, void *user)
        {
            auto &download = *static_cast<Download *>(user);
            const auto bytes = size * count;
            if (bytes > download.maximum - std::min(download.maximum, download.bytes.size()))
            {
                download.exceeded = true;
                return 0;
            }
            download.bytes.append(static_cast<const char *>(contents), bytes);
            return bytes;
        }

        struct CurlCleanup
        {
            CurlCleanup()
            {
                if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                    throw std::runtime_error("Cannot initialize the HTTP client");
            }
            ~CurlCleanup() { curl_global_cleanup(); }
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

    std::string System::read_standard_input(const std::size_t maximum_bytes) const
    {
        if (maximum_bytes != std::numeric_limits<std::size_t>::max())
        {
            std::string input(maximum_bytes + 1, '\0');
            std::cin.read(input.data(), static_cast<std::streamsize>(input.size()));
            input.resize(static_cast<std::size_t>(std::cin.gcount()));
            return input;
        }
        return {std::istreambuf_iterator<char>{std::cin}, {}};
    }

    void System::write_text_file_atomically(const std::filesystem::path &path,
                                            const std::string_view contents) const
    {
        AtomicTextFile{path}.commit(contents);
    }

    HttpResource System::fetch_http(const std::string_view url,
                                    const std::size_t maximum_bytes) const
    {
        static const CurlCleanup curl_cleanup;
        (void)curl_cleanup;
        const auto handle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>{
            curl_easy_init(), curl_easy_cleanup};
        if (!handle)
            throw std::runtime_error("Cannot create an HTTP request");

        const std::string target{url};
        Download download{{}, maximum_bytes};
        char error[CURL_ERROR_SIZE]{};
        curl_easy_setopt(handle.get(), CURLOPT_URL, target.c_str());
        curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(handle.get(), CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 3L);
        curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT_MS, 5000L);
        curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, 15000L);
        curl_easy_setopt(handle.get(), CURLOPT_USERAGENT,
                         "EmailMarkup/1.1 image-embedder");
        curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER, error);
        curl_easy_setopt(handle.get(), CURLOPT_OPENSOCKETFUNCTION, open_public_socket);
        curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &download);
        curl_easy_setopt(handle.get(), CURLOPT_MAXFILESIZE_LARGE,
                         static_cast<curl_off_t>(maximum_bytes));

        const auto result = curl_easy_perform(handle.get());
        if (download.exceeded)
            throw std::runtime_error("image exceeds the " +
                                     std::to_string(maximum_bytes) +
                                     "-byte download limit");
        if (result != CURLE_OK)
            throw std::runtime_error(error[0] ? error : curl_easy_strerror(result));
        long status = 0;
        curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
        if (status < 200 || status >= 300)
            throw std::runtime_error("HTTP response was " + std::to_string(status));
        char *content_type = nullptr;
        curl_easy_getinfo(handle.get(), CURLINFO_CONTENT_TYPE, &content_type);
        return {content_type ? content_type : "", std::move(download.bytes)};
    }
} // namespace email_markup::platform

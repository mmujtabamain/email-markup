#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/platform/system.hpp"

namespace
{
    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            path_ = std::filesystem::temp_directory_path() /
                    ("email-markup-platform-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(path_);
        }

        TemporaryDirectory(const TemporaryDirectory &) = delete;
        TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        [[nodiscard]] const std::filesystem::path &path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };
} // namespace

TEST_CASE("platform system decodes file URIs")
{
    const email_markup::platform::System system{"email-markup-tests"};
    const auto path = system.path_from_file_uri("file:///tmp/Email%20Markup/message.em");

    REQUIRE(path.filename() == "message.em");
    REQUIRE(path.parent_path().filename() == "Email Markup");
}

TEST_CASE("platform system replaces text files atomically")
{
    const email_markup::platform::System system{"email-markup-tests"};
    const TemporaryDirectory temporary;
    const auto output = temporary.path() / "nested/output.html";

    system.write_text_file_atomically(output, "first");
    REQUIRE(system.read_text_file(output) == "first");

    system.write_text_file_atomically(output, "second");
    REQUIRE(system.read_text_file(output) == "second");

    std::size_t entry_count = 0;
    for ([[maybe_unused]] const auto &entry :
         std::filesystem::directory_iterator(output.parent_path()))
        ++entry_count;
    REQUIRE(entry_count == 1);
}

#pragma once

namespace email_markup::cli
{
    class Application final
    {
    public:
        Application(int argc, const char *const argv[]) noexcept;

        [[nodiscard]] int run() const noexcept;

    private:
        Application(const Application &) = delete;
        Application &operator=(const Application &) = delete;

        int argc_;
        const char *const *argv_;
    };
} // namespace email_markup::cli

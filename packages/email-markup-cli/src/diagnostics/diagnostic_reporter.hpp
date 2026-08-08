#pragma once

#include <nlohmann/json_fwd.hpp>

#include "email-markup/core/render.hpp"

namespace email_markup::cli
{
    class DiagnosticReporter final
    {
    public:
        explicit DiagnosticReporter(bool machine_readable) noexcept;

        DiagnosticReporter(const DiagnosticReporter &) = delete;
        DiagnosticReporter &operator=(const DiagnosticReporter &) = delete;

        void print(const email_markup::CompilationResult &result) const;
        [[nodiscard]] static nlohmann::json serialize(
            const email_markup::CompilationResult &result);

    private:
        bool machine_readable_;
    };
} // namespace email_markup::cli

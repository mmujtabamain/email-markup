#include "email-markup/core/format.hpp"

#include <sstream>

namespace email_markup
{

    std::string format_source(const std::string_view source)
    {
        std::string normalized;
        normalized.reserve(source.size() + 1);
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            if (source[i] == '\r' && i + 1 < source.size() && source[i + 1] == '\n')
                continue;
            normalized.push_back(source[i]);
        }
        std::istringstream stream{normalized};
        std::string line;
        std::string output;
        while (std::getline(stream, line))
        {
            const auto end = line.find_last_not_of(" \t");
            if (end == std::string::npos)
                line.clear();
            else
                line.erase(end + 1);
            output += line;
            output.push_back('\n');
        }
        if (output.empty() && !normalized.empty())
            output = "\n";
        return output;
    }

} // namespace email_markup

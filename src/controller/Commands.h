#pragma once

#include <string>
#include <variant>

namespace cmd {
    struct Cancel {};

    struct OpenFile {};

    struct OpenUrl { std::string url; };

    struct GenRandom {
        uint32_t value;
        enum class Type : uint8_t { Kb = 0, Mb, Gb, Lines } type;
    };

    struct SaveAs {};

    struct Close {};
}

using Command = std::variant<
    cmd::Cancel, cmd::OpenFile, cmd::OpenUrl, cmd::GenRandom, cmd::SaveAs, cmd::Close
>;

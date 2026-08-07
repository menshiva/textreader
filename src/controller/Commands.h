#pragma once

#include <string>
#include <cstdint>
#include <variant>

namespace cmd {
    struct OpenFile {};

    struct OpenUrl { std::string url; };

    struct GenRandom {
        enum class Type : uint8_t { Kb = 0, Mb, Gb, Lines };
        uint32_t value;
        Type type;
    };

    struct CancelGenRandom {};

    struct SaveAs {};

    struct Close {};
}

using Command = std::variant<
    cmd::OpenFile, cmd::OpenUrl, cmd::GenRandom, cmd::CancelGenRandom, cmd::SaveAs, cmd::Close
>;

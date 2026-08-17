#pragma once

#include <string>
#include <variant>

namespace cmd {
    struct OpenFile {};
    struct OpenUrl {
        std::string url;
    };
    struct GenRandom {
        uint32_t value;
        enum class Type : uint8_t { Kb = 0, Mb, Gb, Lines } type;
    };
    struct SaveAs {};
    struct Close {};
    struct Find {
        std::string needle;
        bool backwards = false;
        uint64_t fromLineIdx = 0;
        bool continueFromCurrentMatch = false;
    };
    struct CancelFind {};
}

using Command = std::variant<cmd::OpenFile, cmd::OpenUrl, cmd::GenRandom, cmd::SaveAs, cmd::Close, cmd::Find, cmd::CancelFind>;

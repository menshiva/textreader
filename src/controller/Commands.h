#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace cmd {
    struct OpenFile {};
    struct OpenUrl {
        std::string url;
    };
    struct GenRandom {
        enum class Type : uint8_t { Kb = 0, Mb, Gb, Lines };
        static constexpr const char* kTypeNames[] = {"Kb", "Mb", "Gb", "Lines"};
        static constexpr int kTypeCount = std::size(kTypeNames);

        uint32_t value;
        Type type;
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

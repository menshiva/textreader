#pragma once

#include <string>
#include <cstdint>
#include <variant>

namespace cmd {
    struct OpenFile {};
    struct OpenUrl { std::string url; };
    struct GenRandom { int64_t lines; };
    struct SaveAs {};
    struct Close {};
}

using Command = std::variant<
    cmd::OpenFile, cmd::OpenUrl, cmd::GenRandom,
    cmd::SaveAs, cmd::Close
>;

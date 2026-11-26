/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "update_check.hpp"
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

namespace a11y::updates {
namespace {

/* A JSON value, enough of it for a release list. The base port's parser in
 * updater.cpp takes an object for a one-release list, so a refusal such as
 * GitHub's rate limit ({"message": ...}) would end in "up to date"; it also
 * skips the drafts' flag and \u escapes, and lives where fork code cannot
 * reach it. So the fork reads the list itself. */
struct Json {
    enum class Type { null, boolean, number, string, array, object };
    Type type = Type::null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* find(std::string_view key) const {
        for (const auto& [name, value] : members) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }
    std::string text(std::string_view key) const {
        const Json* value = find(key);
        return value != nullptr && value->type == Type::string ? value->string : std::string();
    }
    bool flag(std::string_view key) const {
        const Json* value = find(key);
        return value != nullptr && value->type == Type::boolean && value->boolean;
    }
};

/* Strict enough to refuse a cut-off or garbled list, so it is reported
 * rather than taken as having no release. Nesting is bounded so a hostile
 * body cannot exhaust the stack. */
class Parser {
public:
    explicit Parser(std::string_view src) : m_src(src) {}

    bool parse(Json& out) {
        if (!value(out, 0)) {
            return false;
        }
        space();
        return m_pos == m_src.size();
    }

private:
    static constexpr int kMaxDepth = 64;
    std::string_view m_src;
    std::size_t m_pos = 0;

    void space() {
        while (m_pos < m_src.size() && (m_src[m_pos] == ' ' || m_src[m_pos] == '\t' ||
                                           m_src[m_pos] == '\n' || m_src[m_pos] == '\r'))
        {
            ++m_pos;
        }
    }
    bool take(char c) {
        space();
        if (m_pos < m_src.size() && m_src[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }
    bool word(std::string_view w) {
        if (m_src.substr(m_pos, w.size()) != w) {
            return false;
        }
        m_pos += w.size();
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > kMaxDepth) {
            return false;
        }
        space();
        if (m_pos >= m_src.size()) {
            return false;
        }
        const char c = m_src[m_pos];
        if (c == '{') {
            ++m_pos;
            out.type = Json::Type::object;
            if (take('}')) {
                return true;
            }
            do {
                std::string name;
                Json member;
                space();
                if (!string(name) || !take(':') || !value(member, depth + 1)) {
                    return false;
                }
                out.members.emplace_back(std::move(name), std::move(member));
            } while (take(','));
            return take('}');
        }
        if (c == '[') {
            ++m_pos;
            out.type = Json::Type::array;
            if (take(']')) {
                return true;
            }
            do {
                Json item;
                if (!value(item, depth + 1)) {
                    return false;
                }
                out.items.push_back(std::move(item));
            } while (take(','));
            return take(']');
        }
        if (c == '"') {
            out.type = Json::Type::string;
            return string(out.string);
        }
        if (word("true")) {
            out.type = Json::Type::boolean;
            out.boolean = true;
            return true;
        }
        if (word("false")) {
            out.type = Json::Type::boolean;
            return true;
        }
        if (word("null")) {
            return true;
        }
        return number(out);
    }

    bool number(Json& out) {
        const std::size_t start = m_pos;
        while (m_pos < m_src.size() &&
               std::string_view("+-0123456789.eE").find(m_src[m_pos]) != std::string_view::npos)
        {
            ++m_pos;
        }
        if (m_pos == start) {
            return false;
        }
        const std::string digits(m_src.substr(start, m_pos - start));
        char* end = nullptr;
        out.number = std::strtod(digits.c_str(), &end);
        out.type = Json::Type::number;
        return end == digits.c_str() + digits.size();
    }

    bool hex4(std::uint32_t& out) {
        if (m_pos + 4 > m_src.size()) {
            return false;
        }
        const auto [end, ec] =
            std::from_chars(m_src.data() + m_pos, m_src.data() + m_pos + 4, out, 16);
        if (ec != std::errc() || end != m_src.data() + m_pos + 4) {
            return false;
        }
        m_pos += 4;
        return true;
    }

    static void utf8(std::uint32_t code, std::string& out) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }

    bool escape(std::string& out) {
        if (m_pos >= m_src.size()) {
            return false;
        }
        const char e = m_src[m_pos++];
        switch (e) {
        case '"':
        case '\\':
        case '/':
            out += e;
            return true;
        case 'b':
            out += '\b';
            return true;
        case 'f':
            out += '\f';
            return true;
        case 'n':
            out += '\n';
            return true;
        case 'r':
            out += '\r';
            return true;
        case 't':
            out += '\t';
            return true;
        case 'u': {
            std::uint32_t code = 0;
            if (!hex4(code)) {
                return false;
            }
            /* A character past the first 65536 comes as a surrogate pair. */
            if (code >= 0xd800 && code < 0xdc00 && m_src.substr(m_pos, 2) == "\\u") {
                m_pos += 2;
                std::uint32_t low = 0;
                if (!hex4(low) || low < 0xdc00 || low >= 0xe000) {
                    return false;
                }
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
            }
            /* A lone surrogate is no character, and a NUL would cut the text
             * short in the log and the status line: both become U+FFFD. */
            if (code == 0 || (code >= 0xd800 && code < 0xe000)) {
                code = 0xfffd;
            }
            utf8(code, out);
            return true;
        }
        default:
            return false;
        }
    }

    bool string(std::string& out) {
        if (m_pos >= m_src.size() || m_src[m_pos] != '"') {
            return false;
        }
        ++m_pos;
        while (m_pos < m_src.size()) {
            const char c = m_src[m_pos++];
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (!escape(out)) {
                    return false;
                }
            } else {
                out += c;
            }
        }
        return false;
    }
};

using Version = std::array<std::uint32_t, 3>;

/* MAJOR.MINOR.PATCH and nothing else, as a11y.cmake holds FORK_VERSION to. */
std::optional<Version> parse_version(std::string_view text) {
    Version version{};
    const char* at = text.data();
    const char* const end = text.data() + text.size();
    for (std::size_t part = 0; part < version.size(); ++part) {
        if (part > 0) {
            if (at == end || *at != '.') {
                return std::nullopt;
            }
            ++at;
        }
        if (at == end || *at < '0' || *at > '9') {
            return std::nullopt;
        }
        const auto [next, ec] = std::from_chars(at, end, version[part]);
        if (ec != std::errc()) {
            return std::nullopt;
        }
        at = next;
    }
    if (at != end) {
        return std::nullopt;
    }
    return version;
}

std::optional<Version> tag_version(std::string_view tag) {
    if (tag.substr(0, kTagPrefix.size()) != kTagPrefix) {
        return std::nullopt;
    }
    return parse_version(tag.substr(kTagPrefix.size()));
}

/* The release's zip named asset, if it has one with a link. */
const Json* zip_of(const Json& release, std::string_view asset) {
    const Json* assets = release.find("assets");
    if (asset.empty() || assets == nullptr || assets->type != Json::Type::array) {
        return nullptr;
    }
    for (const Json& item : assets->items) {
        if (item.text("name") == asset && !item.text("browser_download_url").empty()) {
            return &item;
        }
    }
    return nullptr;
}

}  // namespace

Pick pick_release(std::string_view json, std::string_view current, std::string_view asset) {
    Pick pick;
    Json root;
    if (!Parser(json).parse(root)) {
        pick.error = "the release list could not be read";
        return pick;
    }
    if (root.type != Json::Type::array) {
        /* GitHub's own message is left out: its rate limit's names the
         * player's IP address, which would reach the status line, the
         * speech and the log players send back. */
        pick.error =
            root.find("message") != nullptr ? kRefused : "the release list could not be read";
        return pick;
    }
    /* A build's own version always parses (a11y.cmake refuses any other);
     * failing that, every release counts as newer. */
    const Version mine = parse_version(current).value_or(Version{});
    std::optional<Version> best;
    const Json* chosen = nullptr;
    const Json* chosen_zip = nullptr;
    for (const Json& release : root.items) {
        if (release.type != Json::Type::object || release.flag("draft") ||
            release.flag("prerelease"))
        {
            continue;
        }
        const std::optional<Version> version = tag_version(release.text("tag_name"));
        if (!version || *version <= mine || (best && *version <= *best)) {
            continue;
        }
        const Json* zip = zip_of(release, asset);
        if (zip == nullptr) {
            continue;
        }
        best = version;
        chosen = &release;
        chosen_zip = zip;
    }
    if (chosen == nullptr) {
        return pick;
    }
    Offer offer;
    offer.tag = chosen->text("tag_name");
    offer.version = version_of_tag(offer.tag);
    offer.name = chosen->text("name");
    offer.page_url = chosen->text("html_url");
    offer.asset_name = chosen_zip->text("name");
    offer.asset_url = chosen_zip->text("browser_download_url");
    if (const Json* size = chosen_zip->find("size");
        size != nullptr && size->type == Json::Type::number && size->number > 0 &&
        size->number < 1e15)  // past that, or infinite, the cast would be undefined
    {
        offer.asset_size = static_cast<std::size_t>(size->number);
    }
    pick.offer = std::move(offer);
    return pick;
}

std::string asset_name(std::string_view platform, std::string_view architecture) {
    if ((platform != "Windows" && platform != "macOS") ||
        (architecture != "x86_64" && architecture != "arm64"))
    {
        return {};
    }
    return "NonVisualMelee-" + std::string(platform) + "-" + std::string(architecture) + ".zip";
}

std::string version_of_tag(std::string_view tag) {
    if (tag.substr(0, kTagPrefix.size()) == kTagPrefix) {
        tag.remove_prefix(kTagPrefix.size());
    }
    return std::string(tag);
}

std::string up_to_date(const BuildVersions& versions) {
    return "Non-Visual Melee is up to date (" + versions.fork + ").";
}

std::string available(std::string_view tag) {
    return "Update available: Non-Visual Melee " + version_of_tag(tag);
}

std::string description(std::string_view tag) {
    return "A newer version of Non-Visual Melee is available (" + version_of_tag(tag) + ").";
}

}  // namespace a11y::updates

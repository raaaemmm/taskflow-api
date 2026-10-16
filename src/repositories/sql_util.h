#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <drogon/drogon.h>

// nullable bind parameter: an empty optional is sent to PostgreSQL as NULL.
// (Drogon's SqlBinder understands std::optional<T>, not std::shared_ptr<T>.)
using Param = std::optional<std::string>;

inline Param param(const std::optional<std::string>& v) { return v; }
inline Param param(const std::string& v) { return v; }
inline Param param(const char* v) { return std::string(v); }

// PostgreSQL array literal. Tags are validated against [a-z0-9_-] so no escaping is needed.
inline std::string pgArray(const std::vector<std::string>& items) {
    std::string out = "{";
    for (size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + items[i];
    return out + "}";
}

// escape LIKE wildcards in user-supplied search text.
inline std::string likeContains(const std::string& s) {
    std::string out = "%";
    for (char c : s) {
        if (c == '\\' || c == '%' || c == '_') out += '\\';
        out += c;
    }
    return out + "%";
}

#define TS_ISO(col, alias) \
    "to_char(" col " AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS.MS\"Z\"') AS " alias

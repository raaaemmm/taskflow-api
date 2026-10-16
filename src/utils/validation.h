#pragma once
#include <drogon/utils/Utilities.h>

#include <algorithm>
#include <charconv>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/api_error.h"
#include "domain/models.h"

namespace validation {

inline const std::unordered_set<std::string> kStatuses{"todo", "in_progress", "done"};
inline const std::unordered_set<std::string> kSortable{"id", "created_at", "updated_at", "due_date", "priority", "title"};

inline std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

inline std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(trim(cur));
    out.erase(std::remove(out.begin(), out.end(), ""), out.end());
    return out;
}

inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

inline std::optional<int> toInt(const std::string& s) {
    int v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || p != s.data() + s.size()) return std::nullopt;
    return v;
}

class Errors {
  public:
    void add(const std::string& field, const std::string& msg) { e_[field] = msg; }
    void throwIfAny() const {
        if (!e_.empty()) throw ApiError::validation(e_);
    }

  private:
    Json::Value e_{Json::objectValue};
};

inline bool isIsoTimestamp(const std::string& s) {
    static const std::regex re(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(:\d{2}(\.\d{1,6})?)?(Z|[+-]\d{2}:\d{2})$)");
    return std::regex_match(s, re);
}

inline bool isTag(const std::string& s) {
    static const std::regex re(R"(^[a-z0-9][a-z0-9_-]{0,31}$)");
    return std::regex_match(s, re);
}

// cursor (opaque to clients)
inline std::string encodeCursor(int64_t id) {
    const std::string s = "v1:" + std::to_string(id);
    return drogon::utils::base64Encode(reinterpret_cast<const unsigned char*>(s.data()), s.size(), true, false);
}
inline std::optional<int64_t> decodeCursor(const std::string& c) {
    const auto s = drogon::utils::base64Decode(c);
    if (s.rfind("v1:", 0) != 0 || s.size() < 4 || s.size() > 24) return std::nullopt;
    int64_t v = 0;
    auto [p, ec] = std::from_chars(s.data() + 3, s.data() + s.size(), v);
    if (ec != std::errc() || p != s.data() + s.size() || v < 0) return std::nullopt;
    return v;
}

// auth
struct Credentials {
    std::string email, password;
};

inline Credentials parseCredentials(const Json::Value& b, bool enforcePasswordPolicy) {
    Errors err;
    Credentials c;
    if (!b["email"].isString()) {
        err.add("email", "is required");
    } else {
        static const std::regex re(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)");
        c.email = lower(trim(b["email"].asString()));
        if (c.email.size() > 254 || !std::regex_match(c.email, re))
            err.add("email", "must be a valid email address");
    }
    if (!b["password"].isString()) {
        err.add("password", "is required");
    } else {
        c.password = b["password"].asString();
        if (c.password.size() > 128)
            err.add("password", "must be at most 128 characters");
        else if (enforcePasswordPolicy && c.password.size() < 10)
            err.add("password", "must be at least 10 characters");
    }
    err.throwIfAny();
    return c;
}

// todo field validators (return false + record error on failure)
inline bool vTitle(const Json::Value& v, Errors& e, std::string& out) {
    if (!v.isString()) return e.add("title", "must be a string"), false;
    out = trim(v.asString());
    if (out.empty() || out.size() > 200) return e.add("title", "must be 1-200 characters"), false;
    return true;
}
inline bool vDescription(const Json::Value& v, Errors& e, std::optional<std::string>& out) {
    if (v.isNull()) return out.reset(), true;
    if (!v.isString() || v.asString().size() > 5000)
        return e.add("description", "must be a string up to 5000 characters or null"), false;
    out = v.asString();
    return true;
}
inline bool vStatus(const Json::Value& v, Errors& e, std::string& out) {
    if (!v.isString() || !kStatuses.count(v.asString()))
        return e.add("status", "must be one of: todo, in_progress, done"), false;
    out = v.asString();
    return true;
}
inline bool vPriority(const Json::Value& v, Errors& e, int& out) {
    if (!v.isInt() || v.asInt() < 1 || v.asInt() > 4)
        return e.add("priority", "must be an integer 1 (low) to 4 (urgent)"), false;
    out = v.asInt();
    return true;
}
inline bool vDue(const Json::Value& v, Errors& e, std::optional<std::string>& out) {
    if (v.isNull()) return out.reset(), true;
    if (!v.isString() || !isIsoTimestamp(v.asString()))
        return e.add("due_date", "must be an ISO-8601 timestamp with timezone, or null"), false;
    out = v.asString();
    return true;
}
inline bool vTags(const Json::Value& v, Errors& e, std::vector<std::string>& out) {
    if (!v.isArray() || v.size() > 10)
        return e.add("tags", "must be an array of at most 10 tags"), false;
    out.clear();
    for (const auto& t : v) {
        auto s = t.isString() ? lower(trim(t.asString())) : std::string();
        if (!isTag(s))
            return e.add("tags", "each tag must match [a-z0-9][a-z0-9_-]{0,31}"), false;
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    }
    return true;
}

inline NewTodo parseNewTodo(const Json::Value& b) {
    Errors e;
    NewTodo t;
    if (b.isMember("title"))
        vTitle(b["title"], e, t.title);
    else
        e.add("title", "is required");
    if (b.isMember("description")) vDescription(b["description"], e, t.description);
    if (b.isMember("status")) vStatus(b["status"], e, t.status);
    if (b.isMember("priority")) vPriority(b["priority"], e, t.priority);
    if (b.isMember("due_date")) vDue(b["due_date"], e, t.dueDate);
    if (b.isMember("tags")) vTags(b["tags"], e, t.tags);
    e.throwIfAny();
    return t;
}

inline TodoPatch parseTodoPatch(const Json::Value& b) {
    Errors e;
    TodoPatch p;
    if (b.isMember("title")) {
        std::string v;
        if (vTitle(b["title"], e, v)) p.title = {true, v};
    }
    if (b.isMember("description")) {
        std::optional<std::string> v;
        if (vDescription(b["description"], e, v)) p.description = {true, v};
    }
    if (b.isMember("status")) {
        std::string v;
        if (vStatus(b["status"], e, v)) p.status = {true, v};
    }
    if (b.isMember("priority")) {
        int v = 0;
        if (vPriority(b["priority"], e, v)) p.priority = {true, v};
    }
    if (b.isMember("due_date")) {
        std::optional<std::string> v;
        if (vDue(b["due_date"], e, v)) p.dueDate = {true, v};
    }
    if (b.isMember("tags")) {
        std::vector<std::string> v;
        if (vTags(b["tags"], e, v)) p.tags = {true, v};
    }
    e.throwIfAny();
    if (p.empty()) throw ApiError::badRequest("No updatable fields supplied");
    return p;
}

// list query: filtering, sorting, pagination
inline TodoQuery parseTodoQuery(const std::unordered_map<std::string, std::string>& params) {
    TodoQuery q;
    Errors e;
    auto get = [&](const char* k) -> std::optional<std::string> {
        auto it = params.find(k);
        if (it == params.end()) return std::nullopt;
        auto v = trim(it->second);
        if (v.empty()) return std::nullopt;
        return v;
    };
    auto intParam = [&](const char* k, int lo, int hi) -> std::optional<int> {
        auto v = get(k);
        if (!v) return std::nullopt;
        auto n = toInt(*v);
        if (!n || *n < lo || *n > hi) {
            e.add(k, "must be an integer between " + std::to_string(lo) + " and " + std::to_string(hi));
            return std::nullopt;
        }
        return n;
    };

    auto& f = q.filter;
    if (auto v = get("status")) {
        for (const auto& s : split(*v, ',')) {
            if (!kStatuses.count(s)) {
                e.add("status", "values must be among: todo, in_progress, done");
                break;
            }
            if (std::find(f.statuses.begin(), f.statuses.end(), s) == f.statuses.end())
                f.statuses.push_back(s);
        }
    }
    f.priorityMin = intParam("priority_min", 1, 4);
    f.priorityMax = intParam("priority_max", 1, 4);
    if (f.priorityMin && f.priorityMax && *f.priorityMin > *f.priorityMax)
        e.add("priority_min", "must not exceed priority_max");
    if (auto v = get("q")) {
        if (v->size() > 100)
            e.add("q", "must be at most 100 characters");
        else
            f.search = *v;
    }
    if (auto v = get("tags")) {
        for (const auto& t : split(*v, ',')) {
            auto s = lower(t);
            if (!isTag(s)) {
                e.add("tags", "invalid tag in list");
                break;
            }
            if (std::find(f.tags.begin(), f.tags.end(), s) == f.tags.end()) f.tags.push_back(s);
        }
    }
    for (const char* key : {"due_after", "due_before"}) {
        if (auto v = get(key)) {
            
            // '+' in a query string decodes to a space; restore it for offsets like +07:00
            std::string s = *v;
            std::replace(s.begin(), s.end(), ' ', '+');
            if (!isIsoTimestamp(s))
                e.add(key, "must be an ISO-8601 timestamp with timezone");
            else
                (std::string(key) == "due_after" ? f.dueAfter : f.dueBefore) = s;
        }
    }
    if (auto v = get("overdue")) {
        if (*v == "true")
            f.overdue = true;
        else if (*v == "false")
            f.overdue = false;
        else
            e.add("overdue", "must be true or false");
    }

    // sort=-priority,due_date  (max 3 fields, id is always the final tiebreaker)
    if (auto v = get("sort")) {
        for (auto field : split(*v, ',')) {
            bool desc = false;
            if (field[0] == '-' || field[0] == '+') {
                desc = field[0] == '-';
                field.erase(0, 1);
            }
            bool dup = std::any_of(q.sort.begin(), q.sort.end(),
                                   [&](const SortField& s) { return s.column == field; });
            if (!kSortable.count(field) || dup || q.sort.size() >= 3) {
                e.add("sort",
                      "use up to 3 distinct fields of: id, created_at, updated_at, due_date, "
                      "priority, title (prefix - for descending)");
                break;
            }
            q.sort.push_back({field, desc});
        }
    }
    if (q.sort.empty()) q.sort.push_back({"created_at", true});

    // pagination
    const auto mode = get("pagination").value_or("offset");
    if (mode != "offset" && mode != "cursor") e.add("pagination", "must be offset or cursor");
    q.cursorMode = mode == "cursor";
    q.pageSize = intParam("page_size", 1, 100).value_or(20);
    if (q.cursorMode) {
        if (q.sort.size() != 1 || (q.sort[0].column != "created_at" && q.sort[0].column != "id"))
            e.add("sort", "cursor pagination supports only sort=created_at or sort=id");
        if (auto c = get("cursor")) {
            q.cursor = decodeCursor(*c);
            if (!q.cursor) e.add("cursor", "is invalid");
        }
    } else {
        q.page = intParam("page", 1, 10000).value_or(1);
    }
    e.throwIfAny();
    return q;
}

}  // namespace validation

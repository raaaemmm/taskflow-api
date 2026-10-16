#pragma once
#include <json/json.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct User {
    int64_t id{0};
    std::string email;
    std::string passwordHash;
    std::string createdAt;
};

struct Todo {
    int64_t id{0};
    std::string title;
    std::optional<std::string> description;
    std::string status;
    int priority{2};
    std::optional<std::string> dueDate;
    std::vector<std::string> tags;
    std::string createdAt, updatedAt;
    std::optional<std::string> completedAt;
};

// a PATCH field: `set` tells "key was present"; an empty `value` means explicit null.
template <typename T>
struct Field {
    bool set{false};
    std::optional<T> value;
};

struct NewTodo {
    std::string title;
    std::optional<std::string> description;
    std::string status{"todo"};
    int priority{2};
    std::optional<std::string> dueDate;
    std::vector<std::string> tags;
};

struct TodoPatch {
    Field<std::string> title, description, status, dueDate;
    Field<int> priority;
    Field<std::vector<std::string>> tags;
    bool empty() const {
        return !(title.set || description.set || status.set || dueDate.set || priority.set || tags.set);
    }
};

struct TodoFilter {
    std::vector<std::string> statuses;  // any-of
    std::optional<int> priorityMin, priorityMax;
    std::optional<std::string> search;  // title/description substring
    std::vector<std::string> tags;      // all-of
    std::optional<std::string> dueAfter, dueBefore;
    std::optional<bool> overdue;
};

struct SortField {
    std::string column;
    bool desc{false};
};

struct TodoQuery {
    TodoFilter filter;
    std::vector<SortField> sort;
    bool cursorMode{false};
    std::optional<int64_t> cursor;
    int page{1};
    int pageSize{20};
};

struct TodoPage {
    std::vector<Todo> items;
    std::optional<int64_t> total;       // offset mode only
    std::optional<int64_t> nextCursor;  // cursor mode only
};

struct TodoStats {
    int64_t total{0}, todo{0}, inProgress{0}, done{0}, overdue{0};
};

inline Json::Value optStr(const std::optional<std::string>& v) {
    return v ? Json::Value(*v) : Json::Value(Json::nullValue);
}

inline Json::Value toJson(const Todo& t) {
    Json::Value j;
    j["id"] = static_cast<Json::Int64>(t.id);
    j["title"] = t.title;
    j["description"] = optStr(t.description);
    j["status"] = t.status;
    j["priority"] = t.priority;
    j["due_date"] = optStr(t.dueDate);
    Json::Value tags(Json::arrayValue);
    for (const auto& tag : t.tags) tags.append(tag);
    j["tags"] = tags;
    j["created_at"] = t.createdAt;
    j["updated_at"] = t.updatedAt;
    j["completed_at"] = optStr(t.completedAt);
    return j;
}

inline Json::Value toJson(const User& u) {
    Json::Value j;
    j["id"] = static_cast<Json::Int64>(u.id);
    j["email"] = u.email;
    j["created_at"] = u.createdAt;
    return j;
}

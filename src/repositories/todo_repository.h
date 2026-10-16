#pragma once
#include <drogon/drogon.h>

#include "domain/models.h"
#include "repositories/sql_util.h"

namespace detail {

// output aliases differ from column names so ORDER BY / WHERE always hit real columns.
inline constexpr const char* kTodoCols =
    "id, title, description, status, priority, " TS_ISO("due_date", "due_date_iso") ", "
    "to_jsonb(tags)::text AS tags_json, " TS_ISO("created_at", "created_at_iso") ", "
    TS_ISO("updated_at", "updated_at_iso") ", " TS_ISO("completed_at", "completed_at_iso");

// $1 user, $2 statuses[], $3/$4 priority range, $5 search, $6 tags[], $7/$8 due range, $9 overdue
inline constexpr const char* kTodoWhere =
    "user_id = $1 "
    "AND ($2::text[] IS NULL OR status = ANY($2::text[])) "
    "AND ($3::int IS NULL OR priority >= $3::int) "
    "AND ($4::int IS NULL OR priority <= $4::int) "
    "AND ($5::text IS NULL OR title ILIKE $5::text OR COALESCE(description, '') ILIKE $5::text) "
    "AND ($6::text[] IS NULL OR tags @> $6::text[]) "
    "AND ($7::timestamptz IS NULL OR due_date >= $7::timestamptz) "
    "AND ($8::timestamptz IS NULL OR due_date <= $8::timestamptz) "
    "AND ($9::boolean IS NULL OR $9::boolean = "
    "(due_date IS NOT NULL AND due_date < now() AND status <> 'done'))";

inline std::optional<std::string> optCol(const drogon::orm::Row& r, const char* col) {
    if (r[col].isNull()) return std::nullopt;
    return r[col].as<std::string>();
}

inline Todo mapTodo(const drogon::orm::Row& r) {
    Todo t;
    t.id = r["id"].as<int64_t>();
    t.title = r["title"].as<std::string>();
    t.description = optCol(r, "description");
    t.status = r["status"].as<std::string>();
    t.priority = r["priority"].as<int>();
    t.dueDate = optCol(r, "due_date_iso");
    t.createdAt = r["created_at_iso"].as<std::string>();
    t.updatedAt = r["updated_at_iso"].as<std::string>();
    t.completedAt = optCol(r, "completed_at_iso");

    Json::Value tags;
    Json::CharReaderBuilder rb;
    std::string errs;
    const auto raw = r["tags_json"].as<std::string>();
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    if (reader->parse(raw.data(), raw.data() + raw.size(), &tags, &errs) && tags.isArray())
        for (const auto& v : tags) t.tags.push_back(v.asString());
    return t;
}
}  // namespace detail

class TodoRepository {
  public:
    drogon::Task<Todo> create(int64_t userId, const NewTodo& t) {
        auto db = drogon::app().getDbClient();
        auto r = co_await db->execSqlCoro(
            std::string("INSERT INTO todos (user_id, title, description, status, priority, "
                        "due_date, tags, completed_at) "
                        "VALUES ($1, $2, $3, $4, $5::smallint, $6::timestamptz, $7::text[], "
                        "CASE WHEN $4 = 'done' THEN now() END) RETURNING ") +
                detail::kTodoCols,
            userId, t.title, param(t.description), t.status, std::to_string(t.priority),
            param(t.dueDate), pgArray(t.tags));
        co_return detail::mapTodo(r[0]);
    }

    drogon::Task<std::optional<Todo>> find(int64_t userId, int64_t id) {
        auto db = drogon::app().getDbClient();
        auto r = co_await db->execSqlCoro(
            std::string("SELECT ") + detail::kTodoCols +
                " FROM todos WHERE user_id = $1 AND id = $2",
            userId, id);
        if (r.empty()) co_return std::nullopt;
        co_return detail::mapTodo(r[0]);
    }

    drogon::Task<std::optional<Todo>> update(int64_t userId, int64_t id, const TodoPatch& p) {
        auto db = drogon::app().getDbClient();
        auto flag = [](bool b) { return std::string(b ? "true" : "false"); };
        Param prio = p.priority.value ? param(std::to_string(*p.priority.value)) : Param{};
        Param tags = p.tags.value ? param(pgArray(*p.tags.value)) : Param{};
        auto r = co_await db->execSqlCoro(
            std::string(
                "UPDATE todos SET "
                "title = CASE WHEN $3::boolean THEN $4::text ELSE title END, "
                "description = CASE WHEN $5::boolean THEN $6::text ELSE description END, "
                "status = CASE WHEN $7::boolean THEN $8::text ELSE status END, "
                "priority = CASE WHEN $9::boolean THEN $10::smallint ELSE priority END, "
                "due_date = CASE WHEN $11::boolean THEN $12::timestamptz ELSE due_date END, "
                "tags = CASE WHEN $13::boolean THEN $14::text[] ELSE tags END, "
                "completed_at = CASE "
                "  WHEN $7::boolean AND $8::text = 'done' AND status <> 'done' THEN now() "
                "  WHEN $7::boolean AND $8::text <> 'done' THEN NULL "
                "  ELSE completed_at END, "
                "updated_at = now() "
                "WHERE user_id = $1 AND id = $2 RETURNING ") +
                detail::kTodoCols,
            userId, id, flag(p.title.set), param(p.title.value), flag(p.description.set),
            param(p.description.value), flag(p.status.set), param(p.status.value),
            flag(p.priority.set), prio, flag(p.dueDate.set), param(p.dueDate.value),
            flag(p.tags.set), tags);
        if (r.empty()) co_return std::nullopt;
        co_return detail::mapTodo(r[0]);
    }

    drogon::Task<bool> remove(int64_t userId, int64_t id) {
        auto db = drogon::app().getDbClient();
        auto r = co_await db->execSqlCoro("DELETE FROM todos WHERE user_id = $1 AND id = $2", userId, id);
        co_return r.affectedRows() > 0;
    }

    drogon::Task<TodoPage> list(int64_t userId, const TodoQuery& q) {
        auto db = drogon::app().getDbClient();
        const auto& f = q.filter;

        Param statuses = f.statuses.empty() ? Param{} : param(pgArray(f.statuses));
        Param pmin = f.priorityMin ? param(std::to_string(*f.priorityMin)) : Param{};
        Param pmax = f.priorityMax ? param(std::to_string(*f.priorityMax)) : Param{};
        Param search = f.search ? param(likeContains(*f.search)) : Param{};
        Param tags = f.tags.empty() ? Param{} : param(pgArray(f.tags));
        Param dueAfter = param(f.dueAfter);
        Param dueBefore = param(f.dueBefore);
        Param overdue = f.overdue ? param(*f.overdue ? "true" : "false") : Param{};

        // column names come from a validated whitelist; values are always bound parameters.
        std::string order;
        const bool primaryDesc = q.sort.front().desc;
        if (q.cursorMode) {
            order = std::string("id ") + (primaryDesc ? "DESC" : "ASC");
        } else {
            for (const auto& s : q.sort) {
                if (!order.empty()) order += ", ";
                order += s.column + (s.desc ? " DESC" : " ASC");
                if (s.column == "due_date") order += " NULLS LAST";
            }
            order += std::string(", id ") + (primaryDesc ? "DESC" : "ASC");
        }

        const int limit = q.cursorMode ? q.pageSize + 1 : q.pageSize;
        const int offset = q.cursorMode ? 0 : (q.page - 1) * q.pageSize;
        Param cursor = q.cursor ? param(std::to_string(*q.cursor)) : Param{};

        const std::string sql = std::string("SELECT ") + detail::kTodoCols + " FROM todos WHERE " +
                                detail::kTodoWhere + " AND ($10::bigint IS NULL OR id " +
                                (primaryDesc ? "<" : ">") + " $10::bigint) ORDER BY " + order +
                                " LIMIT $11::int OFFSET $12::int";

        auto rows = co_await db->execSqlCoro(sql, userId, statuses, pmin, pmax, search, tags, dueAfter, dueBefore, overdue, cursor, std::to_string(limit), std::to_string(offset));
        TodoPage page;
        for (const auto& row : rows) page.items.push_back(detail::mapTodo(row));

        if (q.cursorMode) {
            if (static_cast<int>(page.items.size()) > q.pageSize) {
                page.items.pop_back();
                page.nextCursor = page.items.back().id;
            }
        } else {
            auto c = co_await db->execSqlCoro(
                std::string("SELECT count(*)::bigint AS n FROM todos WHERE ") + detail::kTodoWhere,
                userId, statuses, pmin, pmax, search, tags, dueAfter, dueBefore, overdue);
            page.total = c[0]["n"].as<int64_t>();
        }
        co_return page;
    }

    drogon::Task<TodoStats> stats(int64_t userId) {
        auto db = drogon::app().getDbClient();
        auto r = co_await db->execSqlCoro(
            "SELECT count(*)::bigint AS total, "
            "count(*) FILTER (WHERE status = 'todo')::bigint AS n_todo, "
            "count(*) FILTER (WHERE status = 'in_progress')::bigint AS n_in_progress, "
            "count(*) FILTER (WHERE status = 'done')::bigint AS n_done, "
            "count(*) FILTER (WHERE due_date < now() AND status <> 'done')::bigint AS n_overdue "
            "FROM todos WHERE user_id = $1",
            userId);
        TodoStats s;
        s.total = r[0]["total"].as<int64_t>();
        s.todo = r[0]["n_todo"].as<int64_t>();
        s.inProgress = r[0]["n_in_progress"].as<int64_t>();
        s.done = r[0]["n_done"].as<int64_t>();
        s.overdue = r[0]["n_overdue"].as<int64_t>();
        co_return s;
    }
};

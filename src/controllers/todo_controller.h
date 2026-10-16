#pragma once
#include <drogon/HttpController.h>

#include <cmath>
#include <unordered_map>

#include "app_context.h"
#include "core/http.h"
#include "utils/validation.h"

class TodoController : public drogon::HttpController<TodoController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TodoController::listTodos, "/api/v1/todos", drogon::Get, "JwtAuthFilter");
    ADD_METHOD_TO(TodoController::createTodo, "/api/v1/todos", drogon::Post, "JwtAuthFilter");
    ADD_METHOD_TO(TodoController::getTodo, "/api/v1/todos/{id}", drogon::Get, "JwtAuthFilter");
    ADD_METHOD_TO(TodoController::updateTodo, "/api/v1/todos/{id}", drogon::Patch, "JwtAuthFilter");
    ADD_METHOD_TO(TodoController::deleteTodo, "/api/v1/todos/{id}", drogon::Delete, "JwtAuthFilter");
    ADD_METHOD_TO(TodoController::todoStats, "/api/v1/stats/todos", drogon::Get, "JwtAuthFilter");
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> listTodos(drogon::HttpRequestPtr req) {
        co_return co_await http::guarded([req]() -> drogon::Task<drogon::HttpResponsePtr> {
            std::unordered_map<std::string, std::string> params(req->parameters().begin(), req->parameters().end());
            const auto q = validation::parseTodoQuery(params);
            const auto page = co_await AppContext::get().todos.list(http::userId(req), q);

            Json::Value items(Json::arrayValue);
            for (const auto& t : page.items) items.append(toJson(t));

            Json::Value pg;
            pg["page_size"] = q.pageSize;
            if (q.cursorMode) {
                pg["mode"] = "cursor";
                pg["has_more"] = page.nextCursor.has_value();
                pg["next_cursor"] = page.nextCursor ? Json::Value(validation::encodeCursor(*page.nextCursor)) : Json::Value(Json::nullValue);
            } else {
                const auto total = page.total.value_or(0);
                const auto pages = (total + q.pageSize - 1) / q.pageSize;
                pg["mode"] = "offset";
                pg["page"] = q.page;
                pg["total"] = static_cast<Json::Int64>(total);
                pg["total_pages"] = static_cast<Json::Int64>(pages);
                pg["has_next"] = q.page < pages;
            }
            Json::Value meta;
            meta["pagination"] = pg;
            co_return http::ok(items, meta);
        });
    }

    drogon::Task<drogon::HttpResponsePtr> createTodo(drogon::HttpRequestPtr req) {
        co_return co_await http::guarded([req]() -> drogon::Task<drogon::HttpResponsePtr> {
            auto t = validation::parseNewTodo(http::body(req));
            auto created = co_await AppContext::get().todos.create(http::userId(req), t);
            auto resp = http::ok(toJson(created), Json::nullValue, drogon::k201Created);
            resp->addHeader("Location", "/api/v1/todos/" + std::to_string(created.id));
            co_return resp;
        });
    }

    drogon::Task<drogon::HttpResponsePtr> getTodo(drogon::HttpRequestPtr req, std::string id) {
        co_return co_await http::guarded([req, id]() -> drogon::Task<drogon::HttpResponsePtr> {
            auto t = co_await AppContext::get().todos.get(http::userId(req), http::parseId(id));
            co_return http::ok(toJson(t));
        });
    }

    drogon::Task<drogon::HttpResponsePtr> updateTodo(drogon::HttpRequestPtr req, std::string id) {
        co_return co_await http::guarded([req, id]() -> drogon::Task<drogon::HttpResponsePtr> {
            const auto tid = http::parseId(id);
            auto patch = validation::parseTodoPatch(http::body(req));
            auto t = co_await AppContext::get().todos.update(http::userId(req), tid, patch);
            co_return http::ok(toJson(t));
        });
    }

    drogon::Task<drogon::HttpResponsePtr> deleteTodo(drogon::HttpRequestPtr req, std::string id) {
        co_return co_await http::guarded([req, id]() -> drogon::Task<drogon::HttpResponsePtr> {
            co_await AppContext::get().todos.remove(http::userId(req), http::parseId(id));
            co_return http::noContent();
        });
    }

    drogon::Task<drogon::HttpResponsePtr> todoStats(drogon::HttpRequestPtr req) {
        co_return co_await http::guarded([req]() -> drogon::Task<drogon::HttpResponsePtr> {
            auto s = co_await AppContext::get().todos.stats(http::userId(req));
            Json::Value j;
            j["total"] = static_cast<Json::Int64>(s.total);
            j["todo"] = static_cast<Json::Int64>(s.todo);
            j["in_progress"] = static_cast<Json::Int64>(s.inProgress);
            j["done"] = static_cast<Json::Int64>(s.done);
            j["overdue"] = static_cast<Json::Int64>(s.overdue);
            co_return http::ok(j);
        });
    }
};
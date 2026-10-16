#pragma once
#include <drogon/drogon.h>

#include "core/api_error.h"
#include "repositories/todo_repository.h"

// business rules live here (ownership scoping, not-found semantics); SQL stays in the repository.
class TodoService {
  public:
    explicit TodoService(TodoRepository& repo) : repo_(repo) {}

    drogon::Task<Todo> create(int64_t uid, const NewTodo& t) {
        co_return co_await repo_.create(uid, t);
    }
    drogon::Task<Todo> get(int64_t uid, int64_t id) {
        auto t = co_await repo_.find(uid, id);
        if (!t) throw ApiError::notFound("Todo not found");
        co_return *t;
    }
    drogon::Task<TodoPage> list(int64_t uid, const TodoQuery& q) {
        co_return co_await repo_.list(uid, q);
    }
    drogon::Task<Todo> update(int64_t uid, int64_t id, const TodoPatch& p) {
        auto t = co_await repo_.update(uid, id, p);
        if (!t) throw ApiError::notFound("Todo not found");
        co_return *t;
    }
    drogon::Task<> remove(int64_t uid, int64_t id) {
        if (!co_await repo_.remove(uid, id)) throw ApiError::notFound("Todo not found");
    }
    drogon::Task<TodoStats> stats(int64_t uid) { co_return co_await repo_.stats(uid); }

  private:
    TodoRepository& repo_;
};

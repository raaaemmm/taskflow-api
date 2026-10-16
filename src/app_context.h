#pragma once
#include "repositories/todo_repository.h"
#include "repositories/user_repository.h"
#include "services/auth_service.h"
#include "services/todo_service.h"

// composition root: Drogon instantiates controllers itself, so they reach services through here.
struct AppContext {
    UserRepository users;
    RefreshTokenRepository refreshTokens;
    TodoRepository todoRepo;
    AuthService auth{users, refreshTokens};
    TodoService todos{todoRepo};

    static AppContext& get() {
        static AppContext ctx;
        return ctx;
    }
};

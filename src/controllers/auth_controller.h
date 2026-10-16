#pragma once
#include <drogon/HttpController.h>

#include "app_context.h"
#include "core/http.h"
#include "domain/models.h"
#include "utils/validation.h"

class AuthController : public drogon::HttpController<AuthController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::registerUser, "/api/v1/auth/register", drogon::Post);
    ADD_METHOD_TO(AuthController::login, "/api/v1/auth/login", drogon::Post);
    ADD_METHOD_TO(AuthController::refresh, "/api/v1/auth/refresh", drogon::Post);
    ADD_METHOD_TO(AuthController::logout, "/api/v1/auth/logout", drogon::Post);
    ADD_METHOD_TO(AuthController::me, "/api/v1/auth/me", drogon::Get, "JwtAuthFilter");
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> registerUser(drogon::HttpRequestPtr req) {
        co_return co_await http::guarded([req]() -> drogon::Task<drogon::HttpResponsePtr> {
            auto creds = validation::parseCredentials(http::body(req), true);
            auto res = co_await AppContext::get().auth.registerUser(creds);
            co_return http::ok(authJson(res), Json::nullValue, drogon::k201Created);
        });
    }

    drogon::Task<drogon::HttpResponsePtr> login(drogon::HttpRequestPtr req) {
        co_return co_await http::guarded([req]() -> drogon::Task<drogon::HttpResponsePtr> {
            auto creds = validation::parseCredentials(http::body(req), false);
            auto res = co_await AppContext::get().auth.login(creds);
            co_return http::ok(authJson(res));
        });
    }

    drogon::Task<drogon::HttpResponsePtr> refresh(drogon::HttpRequestPtr req) {
        co_return co_await http::guarded([req]() -> drogon::Task<drogon::HttpResponsePtr> {
            auto res = co_await AppContext::get().auth.refresh(refreshTokenFrom(req));
            co_return http::ok(authJson(res));
        });
    }

    drogon::Task<drogon::HttpResponsePtr> logout(drogon::HttpRequestPtr req) {
        co_return co_await http::guarded([req]() -> drogon::Task<drogon::HttpResponsePtr> {
            co_await AppContext::get().auth.logout(refreshTokenFrom(req));
            co_return http::noContent();
        });
    }

    drogon::Task<drogon::HttpResponsePtr> me(drogon::HttpRequestPtr req) {
        co_return co_await http::guarded([req]() -> drogon::Task<drogon::HttpResponsePtr> {
            auto user = co_await AppContext::get().auth.me(http::userId(req));
            co_return http::ok(toJson(user));
        });
    }

  private:
    static std::string refreshTokenFrom(const drogon::HttpRequestPtr& req) {
        auto b = http::body(req);
        if (!b["refresh_token"].isString() || b["refresh_token"].asString().empty())
            throw ApiError::validation([] {
                Json::Value d;
                d["refresh_token"] = "is required";
                return d;
            }());
        return b["refresh_token"].asString();
    }

    static Json::Value authJson(const AuthResult& r) {
        Json::Value j;
        j["user"] = toJson(r.user);
        j["tokens"]["access_token"] = r.tokens.accessToken;
        j["tokens"]["refresh_token"] = r.tokens.refreshToken;
        j["tokens"]["token_type"] = "Bearer";
        j["tokens"]["expires_in"] = r.tokens.expiresIn;
        return j;
    }
};

#pragma once
#include <drogon/HttpController.h>

#include "core/http.h"

class HealthController : public drogon::HttpController<HealthController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HealthController::live, "/health", drogon::Get);
    ADD_METHOD_TO(HealthController::ready, "/ready", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> live(drogon::HttpRequestPtr) {
        Json::Value j;
        j["status"] = "ok";
        co_return http::json(drogon::k200OK, j);
    }

    // readiness: verifies the database is reachable.
    drogon::Task<drogon::HttpResponsePtr> ready(drogon::HttpRequestPtr) {
        Json::Value j;
        try {
            co_await drogon::app().getDbClient()->execSqlCoro("SELECT 1");
            j["status"] = "ready";
            co_return http::json(drogon::k200OK, j);
        } catch (const std::exception& e) {
            LOG_ERROR << "Readiness check failed: " << e.what();
            j["status"] = "unavailable";
            co_return http::json(drogon::k503ServiceUnavailable, j);
        }
    }
};

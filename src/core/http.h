#pragma once
#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include <algorithm>
#include <cctype>

#include "core/api_error.h"
#include "core/settings.h"

namespace http {

inline drogon::HttpResponsePtr json(drogon::HttpStatusCode code, const Json::Value& body) {
    auto r = drogon::HttpResponse::newHttpJsonResponse(body);
    r->setStatusCode(code);
    return r;
}

inline drogon::HttpResponsePtr ok(Json::Value data, Json::Value meta = Json::nullValue, drogon::HttpStatusCode code = drogon::k200OK) {
    Json::Value b;
    b["data"] = std::move(data);
    if (!meta.isNull()) b["meta"] = std::move(meta);
    return json(code, b);
}

inline drogon::HttpResponsePtr noContent() {
    auto r = drogon::HttpResponse::newHttpResponse();
    r->setStatusCode(drogon::k204NoContent);
    return r;
}

inline drogon::HttpResponsePtr error(const ApiError& e) {
    Json::Value b;
    auto& er = b["error"];
    er["code"] = e.code;
    er["message"] = e.what();
    if (!e.details.isNull()) er["details"] = e.details;
    return json(e.status, b);
}

// security + CORS headers; applied to every response (including early ones from advices).
inline void decorate(const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
    resp->addHeader("X-Content-Type-Options", "nosniff");
    resp->addHeader("Referrer-Policy", "no-referrer");
    resp->addHeader("Cache-Control", "no-store");
    const auto& origins = Settings::get().corsOrigins;
    const auto origin = req->getHeader("Origin");
    if (origin.empty() || origins.empty()) return;
    const bool any = origins == "*";
    if (any || ("," + origins + ",").find("," + origin + ",") != std::string::npos) {
        resp->addHeader("Access-Control-Allow-Origin", any ? "*" : origin);
        resp->addHeader("Vary", "Origin");
        resp->addHeader("Access-Control-Allow-Methods", "GET,POST,PATCH,DELETE,OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Authorization,Content-Type");
        resp->addHeader("Access-Control-Max-Age", "600");
    }
}

inline Json::Value body(const drogon::HttpRequestPtr& req) {
    auto j = req->getJsonObject();
    if (!j || !j->isObject()) throw ApiError::badRequest("Request body must be a JSON object");
    return *j;
}

inline int64_t parseId(const std::string& s) {
    if (s.empty() || s.size() > 18 || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }))
        throw ApiError::badRequest("Invalid id");
    return std::stoll(s);
}

inline int64_t userId(const drogon::HttpRequestPtr& req) {
    return req->attributes()->get<int64_t>("user_id");
}

// runs a handler body, mapping exceptions to JSON error responses.
template <typename F>
drogon::Task<drogon::HttpResponsePtr> guarded(F fn) {
    try {
        co_return co_await fn();
    } catch (const ApiError& e) {
        co_return error(e);
    } catch (const drogon::orm::SqlError& e) {
        
        // SQLSTATE class 22 = data exception (e.g. impossible date) -> client error
        if (e.sqlState().rfind("22", 0) == 0)
            co_return error(ApiError::badRequest("Invalid value in request"));
        LOG_ERROR << "SQL error: " << e.what();
        co_return error(ApiError::internal());
    } catch (const std::exception& e) {
        LOG_ERROR << "Unhandled exception: " << e.what();
        co_return error(ApiError::internal());
    }
}

}  // namespace http

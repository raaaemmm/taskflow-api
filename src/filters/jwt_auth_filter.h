#pragma once
#include <drogon/HttpFilter.h>
#include <strings.h>

#include "core/http.h"
#include "utils/security.h"

class JwtAuthFilter : public drogon::HttpFilter<JwtAuthFilter> {
  public:
    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& deny, drogon::FilterChainCallback&& next) override {
        const std::string h = req->getHeader("Authorization");
        std::optional<int64_t> uid;
        if (h.size() > 7 && strncasecmp(h.c_str(), "Bearer ", 7) == 0)
            uid = security::verifyAccessToken(h.substr(7));
        if (!uid) {
            auto resp = http::error(ApiError::unauthorized("Missing or invalid access token"));
            resp->addHeader("WWW-Authenticate", "Bearer");
            http::decorate(req, resp);
            deny(resp);
            return;
        }
        req->attributes()->insert("user_id", *uid);
        next();
    }
};

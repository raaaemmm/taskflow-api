#pragma once
#include <drogon/HttpTypes.h>
#include <json/json.h>

#include <stdexcept>
#include <string>

// thrown anywhere below the controller layer; translated to a JSON response by http::guarded.
struct ApiError : std::runtime_error {
    drogon::HttpStatusCode status;
    std::string code;
    Json::Value details;

    ApiError(drogon::HttpStatusCode s, std::string c, const std::string& msg, Json::Value d = Json::nullValue) : std::runtime_error(msg), status(s), code(std::move(c)), details(std::move(d)) {}

    static ApiError badRequest(const std::string& m, Json::Value d = Json::nullValue) {
        return {drogon::k400BadRequest, "bad_request", m, std::move(d)};
    }
    static ApiError unauthorized(const std::string& m = "Authentication required") {
        return {drogon::k401Unauthorized, "unauthorized", m};
    }
    static ApiError notFound(const std::string& m = "Resource not found") {
        return {drogon::k404NotFound, "not_found", m};
    }
    static ApiError conflict(const std::string& m) {
        return {drogon::k409Conflict, "conflict", m};
    }
    static ApiError validation(Json::Value d) {
        return {drogon::k422UnprocessableEntity, "validation_failed", "Request validation failed", std::move(d)};
    }
    static ApiError tooMany() {
        return {drogon::k429TooManyRequests, "rate_limited", "Too many requests"};
    }
    static ApiError internal() {
        return {drogon::k500InternalServerError, "internal_error", "Internal server error"};
    }
};

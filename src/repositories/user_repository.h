#pragma once
#include <drogon/drogon.h>

#include "domain/models.h"
#include "repositories/sql_util.h"

namespace detail {
inline constexpr const char* kUserCols =
    "id, email, password_hash, " TS_ISO("created_at", "created_at_iso");

inline User mapUser(const drogon::orm::Row& r) {
    User u;
    u.id = r["id"].as<int64_t>();
    u.email = r["email"].as<std::string>();
    u.passwordHash = r["password_hash"].as<std::string>();
    u.createdAt = r["created_at_iso"].as<std::string>();
    return u;
}
}  // namespace detail

class UserRepository {
  public:

    // returns nullopt when the email is already taken.
    drogon::Task<std::optional<User>> create(const std::string& email, const std::string& hash) {
        auto db = drogon::app().getDbClient();
        auto r = co_await db->execSqlCoro(
            std::string("INSERT INTO users (email, password_hash) VALUES ($1, $2) "
                        "ON CONFLICT (email) DO NOTHING RETURNING ") +
                detail::kUserCols,
            email, hash);
        if (r.empty()) co_return std::nullopt;
        co_return detail::mapUser(r[0]);
    }

    drogon::Task<std::optional<User>> findByEmail(const std::string& email) {
        auto db = drogon::app().getDbClient();
        auto r = co_await db->execSqlCoro(
            std::string("SELECT ") + detail::kUserCols + " FROM users WHERE email = $1", email);
        if (r.empty()) co_return std::nullopt;
        co_return detail::mapUser(r[0]);
    }

    drogon::Task<std::optional<User>> findById(int64_t id) {
        auto db = drogon::app().getDbClient();
        auto r = co_await db->execSqlCoro(
            std::string("SELECT ") + detail::kUserCols + " FROM users WHERE id = $1", id);
        if (r.empty()) co_return std::nullopt;
        co_return detail::mapUser(r[0]);
    }
};

class RefreshTokenRepository {
  public:
    drogon::Task<> store(int64_t userId, const std::string& tokenHash, int ttlDays) {
        auto db = drogon::app().getDbClient();
        co_await db->execSqlCoro(
            "INSERT INTO refresh_tokens (user_id, token_hash, expires_at) "
            "VALUES ($1, $2, now() + ($3::int * interval '1 day'))",
            userId, tokenHash, std::to_string(ttlDays));
    }

    // atomically revokes a valid token and returns its owner (single-use rotation).
    drogon::Task<std::optional<int64_t>> consume(const std::string& tokenHash) {
        auto db = drogon::app().getDbClient();
        auto r = co_await db->execSqlCoro(
            "UPDATE refresh_tokens SET revoked_at = now() "
            "WHERE token_hash = $1 AND revoked_at IS NULL AND expires_at > now() "
            "RETURNING user_id",
            tokenHash);
        if (r.empty()) co_return std::nullopt;
        co_return r[0]["user_id"].as<int64_t>();
    }

    // a revoked token being replayed means it may have been stolen: kill the whole family.
    drogon::Task<> revokeAllIfReused(const std::string& tokenHash) {
        auto db = drogon::app().getDbClient();
        co_await db->execSqlCoro(
            "UPDATE refresh_tokens SET revoked_at = now() "
            "WHERE revoked_at IS NULL AND user_id IN "
            "(SELECT user_id FROM refresh_tokens WHERE token_hash = $1 AND revoked_at IS NOT NULL)",
            tokenHash);
    }
};

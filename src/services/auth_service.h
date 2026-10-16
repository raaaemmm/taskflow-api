#pragma once
#include <drogon/drogon.h>

#include "core/api_error.h"
#include "core/blocking.h"
#include "core/settings.h"
#include "repositories/user_repository.h"
#include "utils/security.h"
#include "utils/validation.h"

struct AuthTokens {
    std::string accessToken, refreshToken;
    int expiresIn{0};
};
struct AuthResult {
    User user;
    AuthTokens tokens;
};

class AuthService {
  public:
    AuthService(UserRepository& users, RefreshTokenRepository& tokens)
        : users_(users), tokens_(tokens) {}

    drogon::Task<AuthResult> registerUser(const validation::Credentials& c) {
        auto hash = co_await runBlocking([pw = c.password] { return security::hashPassword(pw); });
        auto user = co_await users_.create(c.email, hash);
        if (!user) throw ApiError::conflict("Email is already registered");
        co_return co_await issue(*user);
    }

    drogon::Task<AuthResult> login(const validation::Credentials& c) {
        auto user = co_await users_.findByEmail(c.email);

        // always run one Argon2 verification so response time doesn't reveal whether the email exists.
        const std::string hash = user ? user->passwordHash : dummyHash();
        const bool okPw = co_await runBlocking(
            [hash, pw = c.password] { return security::verifyPassword(hash, pw); });
        if (!user || !okPw) throw ApiError::unauthorized("Invalid email or password");
        co_return co_await issue(*user);
    }

    drogon::Task<AuthResult> refresh(const std::string& refreshToken) {
        const auto hash = security::sha256Hex(refreshToken);
        auto uid = co_await tokens_.consume(hash);
        if (!uid) {
            co_await tokens_.revokeAllIfReused(hash);
            throw ApiError::unauthorized("Invalid or expired refresh token");
        }
        auto user = co_await users_.findById(*uid);
        if (!user) throw ApiError::unauthorized("Invalid or expired refresh token");
        co_return co_await issue(*user);
    }

    drogon::Task<> logout(const std::string& refreshToken) {
        co_await tokens_.consume(security::sha256Hex(refreshToken));
    }

    drogon::Task<User> me(int64_t userId) {
        auto user = co_await users_.findById(userId);
        if (!user) throw ApiError::unauthorized();
        co_return *user;
    }

  private:
    drogon::Task<AuthResult> issue(const User& u) {
        const auto& cfg = Settings::get();
        auto refresh = security::randomToken();
        co_await tokens_.store(u.id, security::sha256Hex(refresh), cfg.refreshTtlDays);
        co_return AuthResult{u, {security::issueAccessToken(u.id), refresh, cfg.accessTtlSec}};
    }

    static const std::string& dummyHash() {
        static const std::string h = security::hashPassword("dummy-password-for-timing");
        return h;
    }

    UserRepository& users_;
    RefreshTokenRepository& tokens_;
};

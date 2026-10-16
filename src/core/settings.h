#pragma once
#include <cstdlib>
#include <stdexcept>
#include <string>

// twelve-factor configuration: everything comes from environment variables.
struct Settings {
    std::string dbHost, dbName, dbUser, dbPass;
    unsigned short dbPort{5432};
    size_t dbPoolSize{10};
    unsigned short port{8080};
    std::string jwtSecret, jwtIssuer;
    int accessTtlSec{900};
    int refreshTtlDays{30};
    std::string migrationsDir;
    std::string corsOrigins;
    int rateLimitPerMin{120};
    int authRateLimitPerMin{10};
    bool trustProxy{false};

    static const Settings& get() {
        static const Settings s = load();
        return s;
    }

  private:
    static std::string env(const char* key, const std::string& def = "") {
        const char* v = std::getenv(key);
        return (v && *v) ? std::string(v) : def;
    }
    static int envInt(const char* key, int def) {
        auto v = env(key);
        return v.empty() ? def : std::stoi(v);
    }
    static Settings load() {
        Settings s;
        s.dbHost = env("DB_HOST", "127.0.0.1");
        s.dbPort = static_cast<unsigned short>(envInt("DB_PORT", 5432));
        s.dbName = env("DB_NAME", "todo");
        s.dbUser = env("DB_USER", "todo");
        s.dbPass = env("DB_PASSWORD", "todo");
        s.dbPoolSize = static_cast<size_t>(envInt("DB_POOL_SIZE", 10));
        s.port = static_cast<unsigned short>(envInt("PORT", 8080));
        s.jwtSecret = env("JWT_SECRET");
        if (s.jwtSecret.size() < 32)
            throw std::runtime_error("JWT_SECRET must be set and at least 32 characters");
        s.jwtIssuer = env("JWT_ISSUER", "todo-api");
        s.accessTtlSec = envInt("ACCESS_TTL_SEC", 900);
        s.refreshTtlDays = envInt("REFRESH_TTL_DAYS", 30);
        s.migrationsDir = env("MIGRATIONS_DIR", "./migrations");
        s.corsOrigins = env("CORS_ORIGINS");
        s.rateLimitPerMin = envInt("RATE_LIMIT_PER_MIN", 120);
        s.authRateLimitPerMin = envInt("AUTH_RATE_LIMIT_PER_MIN", 10);
        s.trustProxy = env("TRUST_PROXY") == "true";
        return s;
    }
};

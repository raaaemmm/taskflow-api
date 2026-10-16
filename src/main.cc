#include <drogon/drogon.h>
#include <cstdlib>
#include "controllers/auth_controller.h"
#include "controllers/health_controller.h"
#include "controllers/todo_controller.h"
#include "core/http.h"
#include "core/migrator.h"
#include "core/rate_limiter.h"
#include "core/settings.h"
#include "filters/jwt_auth_filter.h"
#include "utils/security.h"

namespace {
    std::string clientIp(const drogon::HttpRequestPtr& req, bool trustProxy) {
        if (trustProxy) {
            auto xff = req->getHeader("X-Forwarded-For");
            if (!xff.empty()) return xff.substr(0, xff.find(','));
        }
        return req->getPeerAddr().toIp();
    }
}  // namespace

int main() {
    try {
        const auto& cfg = Settings::get();  // fails fast on missing/weak JWT_SECRET
        security::init();

        // Drogon registers a class in its name registry only if className()/classTypeName()
        // is instantiated. Nothing else references the filter, so force it; otherwise routes
        // using "JwtAuthFilter" log "middleware JwtAuthFilter not found" and run unprotected.
        (void)JwtAuthFilter::classTypeName();

        auto& app = drogon::app();
        const char* confPath = std::getenv("APP_CONFIG");
        app.loadConfigFile(confPath ? confPath : "config.json");
        app.addListener("0.0.0.0", cfg.port);
        app.createDbClient("postgresql", cfg.dbHost, cfg.dbPort, cfg.dbName, cfg.dbUser, cfg.dbPass, cfg.dbPoolSize, "", "default", false, "utf8", 10.0);

        // schema migrations on startup (advisory-locked, safe with multiple replicas).
        app.registerBeginningAdvice([] {
            drogon::async_run([]() -> drogon::Task<> {
                try {
                    co_await migrator::run(Settings::get().migrationsDir);
                    LOG_INFO << "Database schema is up to date";
                } catch (const std::exception& e) {
                    LOG_FATAL << "Migration failed: " << e.what();
                    drogon::app().quit();
                }
            });
        });

        // CORS preflight + rate limiting, before routing.
        static RateLimiter limiter;
        app.registerPreRoutingAdvice([](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& respond, drogon::AdviceChainCallback&& next) {
            const auto& s = Settings::get();
            if (req->method() == drogon::Options) {
                auto r = drogon::HttpResponse::newHttpResponse();
                r->setStatusCode(drogon::k204NoContent);
                http::decorate(req, r);
                respond(r);
                return;
            }
            const bool isAuth = req->path().rfind("/api/v1/auth/", 0) == 0;
            const auto key = clientIp(req, s.trustProxy) + (isAuth ? "|auth" : "|api");
            if (!limiter.allow(key, isAuth ? s.authRateLimitPerMin : s.rateLimitPerMin)) {
                auto r = http::error(ApiError::tooMany());
                r->addHeader("Retry-After", "60");
                http::decorate(req, r);
                respond(r);
                return;
            }
            next();
        });

        app.registerPostHandlingAdvice(
            [](const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
                http::decorate(req, resp);
                LOG_INFO << req->methodString() << ' ' << req->path() << " -> "
                         << static_cast<int>(resp->statusCode());
            });

        // The 404 page bypasses the post-handling advice, so add the security headers here.
        auto notFound = http::error(ApiError::notFound("Route not found"));
        http::decorate(drogon::HttpRequest::newHttpRequest(), notFound);
        app.setCustom404Page(notFound);
        app.setExceptionHandler([](const std::exception& e, const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            LOG_ERROR << "Unhandled exception: " << e.what();
            auto r = http::error(ApiError::internal());
            http::decorate(req, r);
            cb(r);
        });

        LOG_INFO << "Starting todo-api on port " << cfg.port;
        app.run();  // SIGINT/SIGTERM trigger graceful shutdown
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal startup error: %s\n", e.what());
        return 1;
    }
    return 0;
}
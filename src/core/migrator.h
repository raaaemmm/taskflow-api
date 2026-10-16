#pragma once
#include <drogon/drogon.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

// applies migrations/*.sql in filename order, each in its own transaction, guarded by an
// advisory lock so several instances can start at once.
namespace migrator {

inline std::vector<std::string> splitStatements(const std::string& sql) {
    std::string cleaned, line;
    std::istringstream in(sql);
    while (std::getline(in, line)) {
        auto p = line.find_first_not_of(" \t");
        if (p != std::string::npos && line.compare(p, 2, "--") == 0) continue;
        cleaned += line + "\n";
    }
    std::vector<std::string> out;
    std::string cur;
    for (char c : cleaned) {
        if (c == ';') {
            if (cur.find_first_not_of(" \t\r\n") != std::string::npos) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (cur.find_first_not_of(" \t\r\n") != std::string::npos) out.push_back(cur);
    return out;
}

inline drogon::Task<> run(const std::string& dir) {
    namespace fs = std::filesystem;
    auto db = drogon::app().getDbClient();
    co_await db->execSqlCoro(
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "version TEXT PRIMARY KEY, applied_at TIMESTAMPTZ NOT NULL DEFAULT now())");

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".sql") files.push_back(e.path());
    std::sort(files.begin(), files.end());

    for (const auto& f : files) {
        const std::string name = f.filename().string();
        auto tx = co_await db->newTransactionCoro();
        try {
            co_await tx->execSqlCoro("SELECT pg_advisory_xact_lock(727274)");
            auto done = co_await tx->execSqlCoro(
                "SELECT 1 FROM schema_migrations WHERE version = $1", name);
            if (!done.empty()) continue;

            std::ifstream in(f);
            std::stringstream ss;
            ss << in.rdbuf();
            for (const auto& stmt : splitStatements(ss.str())) co_await tx->execSqlCoro(stmt);
            co_await tx->execSqlCoro("INSERT INTO schema_migrations (version) VALUES ($1)", name);
            LOG_INFO << "Applied migration " << name;
        } catch (...) {
            tx->rollback();
            throw;
        }
    }
}

}  // namespace migrator

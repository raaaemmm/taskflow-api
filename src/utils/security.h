#pragma once
#include <jwt-cpp/jwt.h>
#include <sodium.h>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/settings.h"

namespace security {

inline void init() {
    if (sodium_init() < 0) throw std::runtime_error("libsodium init failed");
}

// Argon2id via libsodium; the encoded hash embeds salt + parameters.
inline std::string hashPassword(const std::string& pw) {
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(out, pw.c_str(), pw.size(), crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE) != 0)
        throw std::runtime_error("password hashing failed");
    return out;
}

inline bool verifyPassword(const std::string& hash, const std::string& pw) {
    return crypto_pwhash_str_verify(hash.c_str(), pw.c_str(), pw.size()) == 0;
}

inline std::string toHex(const unsigned char* d, size_t n) {
    std::string hex(n * 2 + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), d, n);
    hex.resize(n * 2);
    return hex;
}

inline std::string randomToken(size_t bytes = 32) {
    std::vector<unsigned char> buf(bytes);
    randombytes_buf(buf.data(), bytes);
    return toHex(buf.data(), bytes);
}

inline std::string sha256Hex(const std::string& s) {
    unsigned char h[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(h, reinterpret_cast<const unsigned char*>(s.data()), s.size());
    return toHex(h, sizeof h);
}

inline std::string issueAccessToken(int64_t userId) {
    const auto& cfg = Settings::get();
    const auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_type("JWT")
        .set_issuer(cfg.jwtIssuer)
        .set_subject(std::to_string(userId))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::seconds(cfg.accessTtlSec))
        .sign(jwt::algorithm::hs256{cfg.jwtSecret});
}

inline std::optional<int64_t> verifyAccessToken(const std::string& token) {
    const auto& cfg = Settings::get();
    try {
        auto decoded = jwt::decode(token);
        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{cfg.jwtSecret})
            .with_issuer(cfg.jwtIssuer)
            .leeway(5)
            .verify(decoded);
        return std::stoll(decoded.get_subject());
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace security

# Todo API

A JWT-authenticated REST API for managing todos, written in **modern C++20** with the
[Drogon](https://github.com/drogonframework/drogon) web framework and **PostgreSQL**.

- Register / login with short-lived access tokens and rotating refresh tokens
- Todos with status, priority, due date, and tags
- Filtering, full-text-style search, multi-field sorting
- Offset **and** cursor (keyset) pagination
- Per-user data isolation, input validation, rate limiting, and consistent JSON errors
- Runs with one command using Docker Compose
- Ships with a Postman collection (75 requests, 446 assertions) and unit tests

---

## Table of contents

1. [Quick start](#quick-start)
2. [Configuration](#configuration)
3. [API reference](#api-reference)
4. [Authentication flow](#authentication-flow)
5. [Testing](#testing)
6. [Project structure](#project-structure)
7. [Development](#development)
8. [Design notes](#design-notes)
9. [Production notes](#production-notes)
10. [Troubleshooting](#troubleshooting)

---

## Quick start

**Requirements:** [Docker Desktop](https://www.docker.com/products/docker-desktop/) (Windows, macOS) or Docker Engine + Compose (Linux). Nothing else needs to be installed.

**1. Create a `.env` file** in the project root with a secret of at least 32 characters:

```env
JWT_SECRET=replace-me-with-a-long-random-string-of-32-or-more-characters
```

Generate a random one:

```bash
# macOS / Linux / WSL
openssl rand -hex 32
```
```powershell
# Windows PowerShell
-join ((1..32) | ForEach-Object { '{0:x2}' -f (Get-Random -Maximum 256) })
```

**2. Build and start** (the first build takes several minutes because it compiles Drogon):

```bash
docker compose up --build
```

The API is now on **http://localhost:8080** with PostgreSQL 16 behind it. Database tables are
created automatically on startup.

**3. Check it works:**

```bash
curl http://localhost:8080/health
# {"status":"ok"}
```

**4. Try it:**

<details open>
<summary>PowerShell</summary>

```powershell
$base = "http://localhost:8080/api/v1"

# Register (returns tokens)
$r = Invoke-RestMethod "$base/auth/register" -Method Post -ContentType 'application/json' `
     -Body '{"email":"me@example.com","password":"correct-horse-1"}'
$h = @{ Authorization = "Bearer $($r.data.tokens.access_token)" }

# Create a todo
Invoke-RestMethod "$base/todos" -Method Post -Headers $h -ContentType 'application/json' `
     -Body '{"title":"Buy milk","priority":3,"tags":["home","shopping"]}'

# List todos
(Invoke-RestMethod "$base/todos" -Headers $h).data
```
</details>

<details>
<summary>curl (bash)</summary>

```bash
BASE=http://localhost:8080/api/v1

TOKEN=$(curl -s $BASE/auth/register -H 'Content-Type: application/json' \
  -d '{"email":"me@example.com","password":"correct-horse-1"}' | jq -r .data.tokens.access_token)

curl -s $BASE/todos -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"title":"Buy milk","priority":3,"tags":["home","shopping"]}'

curl -s "$BASE/todos?status=todo&sort=-priority" -H "Authorization: Bearer $TOKEN"
```
</details>

To stop the stack press `Ctrl+C`, or run `docker compose down`. Data is kept in the `pgdata`
Docker volume; add `-v` to `docker compose down -v` to delete it.

---

## Configuration

All configuration is through environment variables (see [`.env.example`](.env.example)).
Docker Compose reads them from `.env`.

| Variable | Default | Description |
|---|---|---|
| `JWT_SECRET` | *(required)* | HMAC secret, **at least 32 characters**. The server refuses to start without it. |
| `JWT_ISSUER` | `todo-api` | `iss` claim of issued tokens |
| `ACCESS_TTL_SEC` | `900` | Access token lifetime (15 minutes) |
| `REFRESH_TTL_DAYS` | `30` | Refresh token lifetime |
| `PORT` | `8080` | HTTP listen port |
| `DB_HOST` / `DB_PORT` | `127.0.0.1` / `5432` | PostgreSQL address (Compose sets `DB_HOST=db`) |
| `DB_NAME` / `DB_USER` / `DB_PASSWORD` | `todo` / `todo` / `todo` | PostgreSQL credentials |
| `DB_POOL_SIZE` | `10` | Connection pool size |
| `MIGRATIONS_DIR` | `./migrations` | Folder with `.sql` migrations |
| `CORS_ORIGINS` | *(empty = off)* | Comma-separated allowed origins, no spaces. `*` allows any origin |
| `RATE_LIMIT_PER_MIN` | `120` | Requests per minute per client IP |
| `AUTH_RATE_LIMIT_PER_MIN` | `10` | Same, but for `/api/v1/auth/*` |
| `TRUST_PROXY` | `false` | Set `true` **only** behind a trusted proxy: uses `X-Forwarded-For` for the client IP |
| `APP_CONFIG` | `config.json` | Path to the Drogon config file (threads, body size limit, gzip, log level) |

---

## API reference

Base URL: `http://localhost:8080`. All API routes are under **`/api/v1`**; the health probes are not.

### Endpoints

| Method | Path | Auth | Description |
|---|---|---|---|
| GET | `/health` | no | Liveness probe |
| GET | `/ready` | no | Readiness probe (checks the database) |
| POST | `/api/v1/auth/register` | no | Create an account, returns user + tokens (`201`) |
| POST | `/api/v1/auth/login` | no | Log in, returns user + tokens |
| POST | `/api/v1/auth/refresh` | no | Exchange a refresh token for a new token pair |
| POST | `/api/v1/auth/logout` | no | Revoke a refresh token (`204`) |
| GET | `/api/v1/auth/me` | yes | The current user |
| GET | `/api/v1/todos` | yes | List todos (filter, sort, paginate) |
| POST | `/api/v1/todos` | yes | Create a todo (`201`) |
| GET | `/api/v1/todos/{id}` | yes | Get one todo |
| PATCH | `/api/v1/todos/{id}` | yes | Partially update a todo |
| DELETE | `/api/v1/todos/{id}` | yes | Delete a todo (`204`) |
| GET | `/api/v1/stats/todos` | yes | Counts by status, plus overdue |

Authenticated requests need the header `Authorization: Bearer <access_token>`.
Every user can only see and change their own todos.

### Response format

Success responses wrap the payload in `data` (lists also carry `meta`):

```json
{ "data": { "id": 1, "title": "Buy milk" } }
```

Errors always look like this:

```json
{
  "error": {
    "code": "validation_failed",
    "message": "Request validation failed",
    "details": { "title": "must be 1-200 characters" }
  }
}
```

| HTTP | `error.code` | When |
|---|---|---|
| 400 | `bad_request` | Malformed JSON, non-numeric id, body is not an object |
| 401 | `unauthorized` | Missing/invalid/expired token, wrong credentials |
| 404 | `not_found` | Unknown route or todo (also when it belongs to another user) |
| 409 | `conflict` | E-mail already registered |
| 422 | `validation_failed` | Invalid fields or query parameters (`details` names each one) |
| 429 | `too_many_requests` | Rate limit exceeded (check the `Retry-After` header) |
| 500 | `internal_error` | Unexpected server error |

### Auth bodies

```json
{ "email": "me@example.com", "password": "correct-horse-1" }
```

- E-mail: valid format, max 254 characters, stored in lowercase.
- Password: **10 to 128 characters**.
- `refresh` and `logout` take `{ "refresh_token": "..." }`.

Register, login and refresh return:

```json
{
  "data": {
    "user":   { "id": 1, "email": "me@example.com", "created_at": "2026-10-16T06:00:00.000Z" },
    "tokens": {
      "access_token": "eyJ...",
      "refresh_token": "5a62...",
      "token_type": "Bearer",
      "expires_in": 900
    }
  }
}
```

### Todo object

```json
{
  "id": 1,
  "title": "Buy milk",
  "description": "2 litres",
  "status": "todo",
  "priority": 3,
  "due_date": "2026-12-01T03:00:00.000Z",
  "tags": ["home", "shopping"],
  "completed_at": null,
  "created_at": "2026-10-16T06:03:53.542Z",
  "updated_at": "2026-10-16T06:03:53.542Z"
}
```

| Field | Rules | Default |
|---|---|---|
| `title` | required, 1-200 characters | |
| `description` | string up to 5000 characters, or `null` | `null` |
| `status` | `todo` \| `in_progress` \| `done` | `todo` |
| `priority` | integer `1` (low) to `4` (urgent) | `2` |
| `due_date` | ISO-8601 **with timezone** (e.g. `2026-12-01T10:00:00+07:00` or `...Z`), or `null`. Returned in UTC | `null` |
| `tags` | up to 10; each 1-32 characters of `a-z 0-9 _ -`, starting with a letter or digit | `[]` |

`completed_at` is managed by the server: set when the status becomes `done`, cleared when it
changes back.

**PATCH is partial.** Send only the fields you want to change. Send `null` explicitly for
`description` or `due_date` to clear them; omitting a field leaves it untouched.

### Listing todos

`GET /api/v1/todos` accepts these query parameters (all optional, all combinable):

| Parameter | Meaning |
|---|---|
| `status=todo,in_progress` | status is any of the listed values |
| `priority_min`, `priority_max` | priority range, `1`-`4` |
| `q=milk` | case-insensitive substring match in title or description (max 100 characters) |
| `tags=work,urgent` | todo must have **all** the listed tags |
| `due_after`, `due_before` | ISO-8601 with timezone. URL-encode `+` as `%2B` |
| `overdue=true\|false` | past its due date and not done |
| `sort=-priority,due_date` | up to 3 fields from `id, created_at, updated_at, due_date, priority, title`; `-` means descending. Default `-created_at` |
| `page`, `page_size` | offset pagination (default, `page_size` defaults to 20, max 100) |
| `pagination=cursor`, `cursor=...` | keyset pagination (see below) |

**Offset pagination** (default) returns totals:

```json
"meta": { "pagination": { "mode": "offset", "page": 1, "page_size": 20, "total": 42, "total_pages": 3, "has_next": true } }
```

**Cursor pagination** (`pagination=cursor`) is stable while rows are being inserted and avoids a
`COUNT` query. Sorting is limited to `created_at` or `id`. Pass the returned `next_cursor` back as
`cursor` to get the next page:

```json
"meta": { "pagination": { "mode": "cursor", "has_more": true, "next_cursor": "djE6Mg" } }
```

```bash
curl "$BASE/todos?pagination=cursor&page_size=10" -H "Authorization: Bearer $TOKEN"
curl "$BASE/todos?pagination=cursor&page_size=10&cursor=djE6Mg" -H "Authorization: Bearer $TOKEN"
```

### Stats

`GET /api/v1/stats/todos`:

```json
{ "data": { "total": 3, "todo": 1, "in_progress": 1, "done": 1, "overdue": 1 } }
```

---

## Authentication flow

1. **Register or log in** to get an access token (JWT, 15 minutes) and a refresh token (opaque, 30 days).
2. Send the access token on every protected request.
3. When it expires, call **`/auth/refresh`** with the refresh token to get a new pair.
   Refresh tokens **rotate**: each one can be used once.
4. **Reuse detection:** presenting an already-used refresh token is treated as theft. The request is
   rejected and **all** of that user's refresh tokens are revoked, so the user must log in again.
5. **`/auth/logout`** revokes the refresh token you send. The access token stays valid until it
   expires, because JWTs are stateless.

Refresh tokens are stored only as hashes. Passwords are hashed with **Argon2id** (libsodium).

---

## Testing

### Unit tests

Validation and query parsing are covered by unit tests (`tests/validation_test.cc`):

```bash
cmake -B build && cmake --build build
cd build && ctest --output-on-failure
```

### Postman / Newman (end-to-end)

The `postman/` folder holds a collection and a local environment that cover every endpoint
(75 requests, 446 assertions): auth, security, validation errors, every filter and sort,
both pagination modes, update/delete semantics, and the token lifecycle.

**In Postman:** import both files, select the **Todo API - Local** environment, start the API,
then right-click the collection and choose **Run**. Run it top to bottom; the folders depend on
each other. Each run registers a fresh user, so it is repeatable.

**From the command line:**

```bash
npm install -g newman
newman run postman/todo-api.postman_collection.json -e postman/todo-api.local.postman_environment.json
```

> **Rate limit:** one run uses 9 of the 10 auth requests allowed per minute. Running it again
> within a minute returns `429`. For local development, raise the limits in `docker-compose.yml`:
>
> ```yaml
> api:
>   environment:
>     AUTH_RATE_LIMIT_PER_MIN: 1000
>     RATE_LIMIT_PER_MIN: 5000
> ```

---

## Project structure

```
src/
  main.cc               startup: config, DB pool, migrations, advices (rate limit, CORS, logging)
  app_context.h         wires repositories -> services
  controllers/          thin HTTP layer: auth, todos, health
  services/             business rules: auth flows, ownership / not-found handling
  repositories/         all SQL (parameterised): users, refresh tokens, todos
  filters/              JwtAuthFilter (protects routes)
  domain/models.h       entities and query types, no framework logic
  utils/                validation.h (input rules, query parsing), security.h (Argon2id, JWT)
  core/                 settings, errors, HTTP helpers, rate limiter, migrator, blocking worker pool
migrations/             versioned .sql files, applied at startup
tests/                  unit tests
postman/                Postman collection + environment
Dockerfile              multi-stage build (Drogon built statically, slim non-root runtime image)
docker-compose.yml      API + PostgreSQL 16
config.json             Drogon settings
```

Dependency direction: **controllers -> services -> repositories -> database.**
Domain and validation code know nothing about HTTP.

---

## Development

### Docker (recommended on every OS, required in practice on Windows)

```bash
docker compose up --build
```

Rebuilds reuse cached layers, so after the first build only your code is recompiled (about 20-30
seconds). **Save your files before rebuilding**: Docker builds from what is on disk.

### Native build (Linux, macOS, or WSL2 on Windows)

Install a C++20 compiler, CMake 3.20+, and:
[Drogon](https://github.com/drogonframework/drogon) **v1.9.x** (built with PostgreSQL),
libpq, libsodium, OpenSSL, jsoncpp, uuid, zlib, c-ares. On Ubuntu/Debian:

```bash
sudo apt install build-essential cmake git pkg-config libjsoncpp-dev uuid-dev zlib1g-dev \
                 libssl-dev libpq-dev libsodium-dev libc-ares-dev postgresql

git clone --depth 1 --branch v1.9.10 --recurse-submodules https://github.com/drogonframework/drogon.git
cmake -S drogon -B drogon/build -DBUILD_POSTGRESQL=ON -DBUILD_MYSQL=OFF -DBUILD_SQLITE=OFF \
      -DBUILD_REDIS=OFF -DBUILD_EXAMPLES=OFF
cmake --build drogon/build -j && sudo cmake --install drogon/build
```

Create the database, then build and run **from the project root** (it looks for `config.json`
and `./migrations` in the current directory):

```bash
sudo -u postgres psql -c "CREATE USER todo WITH PASSWORD 'todo';" -c "CREATE DATABASE todo OWNER todo;"

cmake -B build && cmake --build build -j
export JWT_SECRET=$(openssl rand -hex 32)
./build/todo_api
```

`cmake -B build -DENABLE_SANITIZERS=ON` builds with AddressSanitizer + UBSan (GCC/Clang).
`jwt-cpp` is downloaded automatically by CMake.

### Windows + VS Code

- Build and run with Docker Desktop as above.
- Red squiggles on `#include <drogon/drogon.h>` are only IntelliSense failing to find the Drogon
  headers on Windows. They are not compile errors. For full IntelliSense, open the project in
  **WSL2** with the VS Code *WSL* extension and install Drogon there (see above).
- Formatting uses **clang-format** via the *C/C++* extension. Add a `.clang-format` file and set
  `"editor.defaultFormatter": "ms-vscode.cpptools"` and `"C_Cpp.clang_format_style": "file"`.

### Adding a migration

Add a new file such as `migrations/002_add_something.sql`. Files are applied in name order, once
each, inside one transaction per file, guarded by a PostgreSQL advisory lock (safe with several
replicas). The migrator splits statements on `;`, so **avoid `DO $$ ... $$` blocks and semicolons
inside strings.**

---

## Design notes

- **Header-only layout.** Only `src/main.cc` is compiled; every other `.h` is included into it, so
  functions in headers are marked `inline`. This keeps the build simple. If rebuilds get slow, move
  implementations into `.cc` files and list them in `add_executable`.
- **Filters are middlewares.** In Drogon 1.9.x a filter class is only registered by name if
  something instantiates `className()`/`classTypeName()`. `main()` calls
  `JwtAuthFilter::classTypeName()` for that reason. Without it, routes log
  `middleware JwtAuthFilter not found` and run unprotected.
- **Argon2id never blocks the event loop.** Password hashing runs on a separate worker pool.
- **All SQL is parameterised**, every query is scoped by `user_id`, and sort columns come from a
  fixed whitelist. Nullable parameters use `std::optional<std::string>`.
- **Security headers** (`X-Content-Type-Options`, `Cache-Control: no-store`,
  `Referrer-Policy`) are added to every response, including errors and 404s.

---

## Production notes

- Set a strong, unique `JWT_SECRET` (32+ characters) and change the database password. Never
  commit `.env`.
- Rate limiting is **per instance and in memory**. Use a gateway or load balancer for global
  limits, and set `TRUST_PROXY=true` only when behind a proxy you control.
- Put TLS in front of the API (reverse proxy or load balancer); the app speaks plain HTTP.
- Set `CORS_ORIGINS` to the exact origins of your frontend instead of `*`.
- The container runs as a non-root user (uid 10001).
- Not included: e-mail verification, password reset, and a `pg_trgm` index for fast substring
  search on very large tables.

---

## Troubleshooting

| Problem | Cause and fix |
|---|---|
| `JWT_SECRET must be set and at least 32 characters` | Add `JWT_SECRET` to `.env` (or export it) and make sure it is 32+ characters |
| `docker compose` says `set JWT_SECRET` | Compose reads `.env` from the project folder; check it exists and has no typos |
| My code change had no effect | The file was not saved, so Docker reused the cached layer. Save (`Ctrl+S`) and run `docker compose up --build` again |
| `middleware JwtAuthFilter not found` in the log | See *Design notes*: `main.cc` must call `JwtAuthFilter::classTypeName()` |
| `Permission denied creating ./uploads/tmp/...` | The `Dockerfile` must create `/app/uploads` and `chown` it to uid 10001 |
| `429 Too Many Requests` | Rate limit hit. Wait 60 seconds, or raise `AUTH_RATE_LIMIT_PER_MIN` / `RATE_LIMIT_PER_MIN` |
| `/api/v1/health` returns 404 | Health probes are `/health` and `/ready`, with no `/api/v1` prefix |
| Red squiggles in VS Code on Windows | Only IntelliSense; see *Windows + VS Code* above |
| Reset the database | `docker compose down -v` deletes the `pgdata` volume |
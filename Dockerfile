# build
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config ca-certificates \
    libjsoncpp-dev uuid-dev zlib1g-dev libssl-dev libpq-dev libsodium-dev libc-ares-dev \
    && rm -rf /var/lib/apt/lists/*

# Drogon, built statically so the runtime image needs no copies of its libs.
RUN git clone --depth 1 --branch v1.9.10 --recurse-submodules https://github.com/drogonframework/drogon.git /drogon \
    && cmake -S /drogon -B /drogon/build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_CTL=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_ORM=ON \
    -DBUILD_POSTGRESQL=ON -DBUILD_MYSQL=OFF -DBUILD_SQLITE=OFF -DBUILD_REDIS=OFF \
    && cmake --build /drogon/build -j"$(nproc)" && cmake --install /drogon/build

WORKDIR /src
COPY CMakeLists.txt config.json ./
COPY src ./src
COPY tests ./tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)" --target todo_api

# runtime
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libpq5 libsodium23 libjsoncpp25 libssl3t64 zlib1g libuuid1 libc-ares2 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --no-create-home app
WORKDIR /app
COPY --from=build /src/build/todo_api /app/todo_api
COPY config.json /app/config.json
COPY migrations /app/migrations
# Drogon creates ./uploads/tmp/* at startup; the non-root user must be able to write there.
RUN mkdir -p /app/uploads && chown -R 10001:10001 /app/uploads
USER 10001
EXPOSE 8080
ENTRYPOINT ["/app/todo_api"]
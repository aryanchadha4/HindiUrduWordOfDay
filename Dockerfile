FROM node:20-bookworm-slim AS frontend-build
WORKDIR /app/frontend
COPY frontend/package.json frontend/package-lock.json* ./
RUN npm install
COPY frontend/ ./
RUN npm run build

FROM debian:bookworm-slim AS backend-build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config libsqlite3-dev libssl-dev ca-certificates git \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app/backend
COPY backend/ ./
# Production image: skip Catch2/tests and limit parallelism for small build VMs (e.g. Render).
ENV CMAKE_BUILD_PARALLEL_LEVEL=2
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_TOOLS=OFF \
    && cmake --build build --target hindiurdu_server

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=backend-build /app/backend/build/hindiurdu_server /app/hindiurdu_server
COPY --from=backend-build /app/backend/data/words.json /app/default-data/words.json
COPY --from=frontend-build /app/frontend/dist /app/frontend/dist
COPY docker/start.sh /app/start.sh
RUN chmod +x /app/start.sh

ENV PORT=8080
EXPOSE 8080
ENTRYPOINT ["/app/start.sh"]

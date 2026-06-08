# Hindi / Urdu Word of the Day

A small static web app with a C++ JSON API: daily vocabulary (meaning, etymology, example), “I already know this” tracking, and fuzzy English quizzes on words you have marked known.

## Prerequisites

- CMake 3.16+, a C++17 compiler, and SQLite 3 development libraries (macOS: Xcode CLT; Linux: `libsqlite3-dev`).
- Node.js 18+ for the frontend dev server and build.

## Backend (C++)

From the repository root:

```bash
cd backend
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the API (paths are relative to your current working directory unless overridden):

```bash
cd build
WORDS_JSON=../data/words.json USER_DB=../data/users.db PORT=8080 ./hindiurdu_server
```

You can also pass arguments explicitly:

```bash
./hindiurdu_server ../data/words.json ../data/users.db 8080
```

Endpoints:

- `GET /api/health` — liveness JSON.
- `GET /api/word-of-day?date=YYYY-MM-DD` — deterministic word for the date, skipping words marked known for the `X-User-Token` header.
- `POST /api/known` with JSON `{ "word_id": "..." }` — persist a known word for that token.
- `GET /api/quiz` — random prompt from known words (meanings omitted).
- `POST /api/quiz/check` with `{ "word_id": "...", "answer": "..." }` — fuzzy match against the English gloss.

## Frontend (Vite)

With the backend listening on port 8080:

```bash
cd frontend
npm install
npm run dev
```

Vite proxies `/api` to `http://127.0.0.1:8080`. Open the URL printed in the terminal (typically `http://localhost:5173`).

Production build:

```bash
npm run build
```

Serve `frontend/dist` behind a reverse proxy that forwards `/api` to the C++ process, or host the API on the same origin.

## Deploy to Render (recommended)

This repo ships a multi-stage [`Dockerfile`](Dockerfile) and [`render.yaml`](render.yaml) blueprint. One container serves the Vite frontend and C++ API on the same origin.

### 1) Push to GitHub

Render deploys from a Git repository. Commit and push this project to GitHub (or GitLab).

### 2) Create the service on Render

**Option A — Blueprint (uses `render.yaml`):**

1. Sign in at [render.com](https://render.com).
2. **New → Blueprint**.
3. Connect the repository and approve the `hindiurdu-word-of-day` web service.

**Option B — Manual Docker service:**

1. **New → Web Service** → connect the repository.
2. **Runtime:** Docker (Render detects the root `Dockerfile`).
3. **Health check path:** `/api/health`
4. Add environment variables (or copy from `render.yaml`):
   - `DATA_DIR` = `/data`
   - `WORDS_JSON` = `/data/words.json`
   - `USER_DB` = `/data/users.db`
   - `STATIC_DIR` = `/app/frontend/dist`
5. **Create Web Service**.

Render sets `PORT` automatically; do not hardcode it in the dashboard.

### 3) Verify

After the first build finishes (a few minutes):

```bash
curl -sS https://<your-service>.onrender.com/api/health
# expect: {"ok":true}
```

Open `https://<your-service>.onrender.com` in a browser.

### Free tier behavior

- Services **spin down after ~15 minutes** of no traffic. The first visit may take 30–60 seconds; the UI shows a loading state and **Retry** if needed.
- The filesystem is **ephemeral** on Free: “known word” progress may reset on redeploy or restart. Upgrade to **Starter** and attach a persistent disk (see below) to keep progress.

### Persistent data (Starter plan)

1. Upgrade the web service to **Starter** in the Render dashboard.
2. **Disks → Add disk**, mount path `/data`, size 1 GB.
3. Uncomment the `disk:` block in [`render.yaml`](render.yaml) and sync the blueprint, or add the disk only in the dashboard.
4. Redeploy.

On first boot, `docker/start.sh` copies `/app/default-data/words.json` to `/data/words.json` if missing and repairs invalid catalogs.

### Custom domain

In the Render service: **Settings → Custom Domains** → add your domain and follow the DNS instructions (TLS is automatic).

### Troubleshooting

- **Build fails:** open **Logs** in Render; confirm the Docker build completes locally with `docker build .` if needed.
- **Health check fails:** ensure `/api/health` returns `{"ok":true}`; check runtime logs for `Failed to load words` or `Failed to open database`.
- **Slow first load:** normal on Free after idle; use **Retry** or upgrade to Starter for always-on.

## Deploy to Fly.io (optional)

[`fly.toml`](fly.toml) is included for Fly.io. Requires an active Fly account (paid after trial). See Fly docs for `fly deploy` if you use that platform instead.

## Data

Curated words live in [backend/data/words.json](backend/data/words.json). User-specific known-word rows are stored in the SQLite file configured by `USER_DB` (default `./data/users.db` next to the process working directory).

### Merging extra vocabulary (C++)

The backend builds a small helper that merges JSON **array** chunk files into `words.json` (UTF-8, indented). Duplicate `id` values are rejected.

```bash
cd backend/build
cmake --build . --target merge_word_catalog
./merge_word_catalog ../data/words.json ../data/chunk_a.json ../data/chunk_b.json
```

Optional flags (same order as in the help text: flags first, then paths):

- `--max-append N` — keep only the first `N` entries collected from the chunks (after concatenating chunk order).
- `--expect-total M` — require the final array length to be exactly `M` or the tool exits with an error.

Example:

```bash
./merge_word_catalog --max-append 141 --expect-total 200 ../data/words.json ../data/add30_1.json ../data/add30_2.json
```

### Appending rows from a TSV (C++)

`append_catalog_tsv` reads a UTF-8 tab-separated file (header row required) and **appends** each data row to `words.json`. Field order:

`id` → `hindi` → `urdu` → `roman` → `pronunciation` → `meaning_en` → `etymology_en` → `example_sentence` → `synonyms_en`

Use **semicolons** in `synonyms_en` between English glosses (no tabs inside any cell). Empty synonyms are allowed.

```bash
cd backend/build
cmake --build . --target append_catalog_tsv
./append_catalog_tsv --expect-rows 100 ../data/words.json ../data/sk_bulk.tsv
```

To regenerate `backend/data/sk_bulk.tsv` (output path is always next to `words.json`, no matter where you run Python from):

```bash
# from repository root (HindiUrduWordOfDay/)
python3 backend/tools/gen_sk_bulk_tsv.py

# or from backend/
python3 tools/gen_sk_bulk_tsv.py
```

`--expect-rows N` is optional; it fails unless exactly `N` non-empty data rows are read after the header.

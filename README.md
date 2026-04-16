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

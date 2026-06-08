#!/usr/bin/env sh
set -eu

DATA_DIR="${DATA_DIR:-/data}"
WORDS_JSON="${WORDS_JSON:-${DATA_DIR}/words.json}"
USER_DB="${USER_DB:-${DATA_DIR}/users.db}"
STATIC_DIR="${STATIC_DIR:-/app/frontend/dist}"
PORT="${PORT:-8080}"
DEFAULT_WORDS="/app/default-data/words.json"

mkdir -p "${DATA_DIR}"

if [ ! -w "${DATA_DIR}" ]; then
  echo "ERROR: ${DATA_DIR} is not writable" >&2
  exit 1
fi

words_json_ok() {
  # Shell-only check: non-empty file that looks like a JSON array (no Python in runtime image).
  [ -f "$1" ] && [ -s "$1" ] || return 1
  case "$(sed 's/^[[:space:]]*//' "$1" | head -c 1)" in
    '[') return 0 ;;
    *) return 1 ;;
  esac
}

repair_words_json() {
  reason="$1"
  if [ -f "${WORDS_JSON}" ]; then
    ts=$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || echo backup)
    mv "${WORDS_JSON}" "${WORDS_JSON}.bak.${ts}"
    echo "WARN: ${reason}; backed up and restoring default words.json" >&2
  else
    echo "WARN: ${reason}; copying default words.json" >&2
  fi
  cp "${DEFAULT_WORDS}" "${WORDS_JSON}"
}

if [ ! -f "${DEFAULT_WORDS}" ]; then
  echo "ERROR: missing default catalog at ${DEFAULT_WORDS}" >&2
  exit 1
fi

if [ ! -f "${WORDS_JSON}" ]; then
  cp "${DEFAULT_WORDS}" "${WORDS_JSON}"
elif ! words_json_ok "${WORDS_JSON}"; then
  repair_words_json "invalid or empty ${WORDS_JSON}"
fi

if ! words_json_ok "${WORDS_JSON}"; then
  echo "ERROR: could not initialize valid words.json at ${WORDS_JSON}" >&2
  exit 1
fi

touch "${USER_DB}"

export WORDS_JSON
export USER_DB
export STATIC_DIR
export PORT

exec /app/hindiurdu_server

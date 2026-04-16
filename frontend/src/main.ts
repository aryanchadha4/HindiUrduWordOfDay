import "./style.css";

type Word = {
  id: string;
  hindi: string;
  urdu: string;
  roman: string;
  pronunciation: string;
  meaning_en: string;
  etymology_en: string;
  example_sentence: string;
  synonyms_en: string[];
};

type WordOfDayResponse = { word: Word | null; message?: string; known_count?: number };
type QuizPrompt = {
  word_id: string;
  hindi: string;
  urdu: string;
  roman: string;
  pronunciation: string;
};
type QuizResponse = { prompt: QuizPrompt | null; message?: string };
type QuizCheckResponse = {
  correct: boolean;
  similarity: number;
  expected_hint?: string;
};

const TOKEN_KEY = "hindiurdu_user_token";
const QUIZ_HIDE_KEY = "hindiurdu_quiz_hidden";

function getOrCreateToken(): string {
  let t = localStorage.getItem(TOKEN_KEY);
  if (!t) {
    t = crypto.randomUUID();
    localStorage.setItem(TOKEN_KEY, t);
  }
  return t;
}

async function apiGet<T>(path: string): Promise<T> {
  const res = await fetch(path, {
    headers: { "X-User-Token": getOrCreateToken() },
  });
  if (!res.ok) {
    throw new Error(`${res.status} ${await res.text()}`);
  }
  return (await res.json()) as T;
}

async function apiPost<T>(path: string, body: unknown): Promise<T> {
  const res = await fetch(path, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "X-User-Token": getOrCreateToken(),
    },
    body: JSON.stringify(body),
  });
  if (!res.ok) {
    throw new Error(`${res.status} ${await res.text()}`);
  }
  return (await res.json()) as T;
}

type ScriptMode = "hindi" | "roman" | "urdu";

function nextScriptMode(m: ScriptMode): ScriptMode {
  if (m === "hindi") return "roman";
  if (m === "roman") return "urdu";
  return "hindi";
}

function escapeHtml(s: string) {
  return s
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function wordDisplayText(w: { hindi: string; urdu: string; roman: string }, mode: ScriptMode) {
  if (mode === "hindi") return w.hindi;
  if (mode === "roman") return w.roman;
  return w.urdu;
}

function wordLineClass(mode: ScriptMode) {
  if (mode === "hindi") return "word-line devanagari";
  if (mode === "urdu") return "word-line urdu";
  return "word-line roman";
}

function mountApp() {
  const root = document.querySelector<HTMLDivElement>("#app");
  if (!root) return;

  let word: Word | null = null;
  let loadError: string | null = null;
  let flipped = false;
  let scriptMode: ScriptMode = "hindi";
  let quizPrompt: QuizPrompt | null = null;
  let quizMessage: string | null = null;
  let quizScriptMode: ScriptMode = "hindi";
  let quizResult: QuizCheckResponse | null = null;
  let quizUiHidden = localStorage.getItem(QUIZ_HIDE_KEY) === "1";
  let knownCount = 0;

  root.innerHTML = `
    <div class="page">
      <header>
        <h1>Hindi / Urdu word of the day</h1>
        <p>Flip the card for meaning and etymology. Tap the word to cycle Hindi → roman → Urdu.</p>
      </header>
      <div id="banner"></div>
      <div class="scene" id="scene"></div>
      <section class="quiz-strip" id="quizPanel" aria-label="Quiz">
        <p class="quiz-lead">
          <strong>Quiz</strong> Type the English meaning. One word per visit, picked at random from words you marked known.
          <button type="button" id="btnToggleQuiz" class="secondary quiz-inline-btn">Hide quiz</button>
        </p>
        <div id="quizCollapsible">
          <div id="quizBody"></div>
        </div>
      </section>
    </div>
  `;

  const banner = root.querySelector<HTMLDivElement>("#banner");
  const scene = root.querySelector<HTMLDivElement>("#scene");
  const quizBody = root.querySelector<HTMLDivElement>("#quizBody");
  const quizCollapsible = root.querySelector<HTMLDivElement>("#quizCollapsible");
  const btnToggleQuiz = root.querySelector<HTMLButtonElement>("#btnToggleQuiz");

  function applyQuizVisibility() {
    if (quizCollapsible) {
      quizCollapsible.style.display = quizUiHidden ? "none" : "";
    }
    if (btnToggleQuiz) {
      btnToggleQuiz.textContent = quizUiHidden ? "Show quiz" : "Hide quiz";
      btnToggleQuiz.setAttribute("aria-expanded", quizUiHidden ? "false" : "true");
    }
  }

  btnToggleQuiz?.addEventListener("click", () => {
    quizUiHidden = !quizUiHidden;
    if (quizUiHidden) {
      localStorage.setItem(QUIZ_HIDE_KEY, "1");
    } else {
      localStorage.removeItem(QUIZ_HIDE_KEY);
    }
    applyQuizVisibility();
  });
  applyQuizVisibility();

  function paintBanner() {
    if (!banner) return;
    banner.innerHTML = loadError ? `<div class="error-banner" role="alert">${escapeHtml(loadError)}</div>` : "";
  }

  function paintScene() {
    if (!scene) return;
    if (!word) {
      scene.innerHTML = `<p class="empty-state">No word available. Clear site data to reset known words, or expand the catalog.</p>`;
      return;
    }
    scene.innerHTML = `
      <div class="card" id="flipCard" aria-label="Word card. Tap away from the word to flip.">
        <div class="card-inner ${flipped ? "is-flipped" : ""}" id="cardInner">
          <div class="card-face card-front">
            <span class="known-counter" aria-label="Words you marked as known">${knownCount}</span>
            <p class="hint">Tap outside the word on the card to flip it over.</p>
            <div class="card-hero">
              <div class="pronunciation">${escapeHtml(word.pronunciation)}</div>
              <div class="${wordLineClass(scriptMode)}" id="wordLine" role="button" tabindex="0">${escapeHtml(
                wordDisplayText(word, scriptMode),
              )}</div>
            </div>
            <div class="actions">
              <button type="button" id="btnKnown">I already know this word</button>
            </div>
          </div>
          <div class="card-face card-back">
            <span class="known-counter" aria-label="Words you marked as known">${knownCount}</span>
            <p class="hint">Tap the card to flip back.</p>
            <div class="back-block">
              <h2>Meaning</h2>
              <p>${escapeHtml(word.meaning_en)}</p>
            </div>
            <div class="back-block">
              <h2>Etymology</h2>
              <p>${escapeHtml(word.etymology_en)}</p>
            </div>
            <div class="back-block">
              <h2>In a sentence</h2>
              <p>${escapeHtml(word.example_sentence)}</p>
            </div>
          </div>
        </div>
      </div>
    `;

    const cardInner = scene.querySelector<HTMLDivElement>("#cardInner");
    const wordLine = scene.querySelector<HTMLDivElement>("#wordLine");

    cardInner?.addEventListener("click", (e) => {
      const t = e.target as HTMLElement | null;
      if (!t) return;
      if (t.closest("#wordLine")) return;
      flipped = !flipped;
      cardInner.classList.toggle("is-flipped", flipped);
    });

    wordLine?.addEventListener("click", (e) => {
      e.stopPropagation();
      scriptMode = nextScriptMode(scriptMode);
      paintScene();
    });
    wordLine?.addEventListener("keydown", (e) => {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        e.stopPropagation();
        scriptMode = nextScriptMode(scriptMode);
        paintScene();
      }
    });

    scene.querySelector("#btnKnown")?.addEventListener("click", async () => {
      if (!word) return;
      try {
        await apiPost("/api/known", { word_id: word.id });
        await refreshWord();
        flipped = false;
        scriptMode = "hindi";
        paintBanner();
        paintScene();
        await ensureQuizIfStillEmpty();
        paintQuiz();
      } catch (err) {
        loadError = err instanceof Error ? err.message : "Could not save.";
        paintBanner();
      }
    });
  }

  function paintQuiz() {
    if (!quizBody) return;
    if (quizMessage && !quizPrompt) {
      quizBody.innerHTML = `<p class="status">${escapeHtml(quizMessage)}</p>`;
      return;
    }
    if (!quizPrompt) {
      quizBody.innerHTML = `<p class="status">Mark at least one word as known, then refresh the page to get a quiz word.</p>`;
      return;
    }

    quizBody.innerHTML = `
      <p class="pronunciation">${escapeHtml(quizPrompt.pronunciation)}</p>
      <div class="${wordLineClass(quizScriptMode)}" id="quizWordLine" role="button" tabindex="0">${escapeHtml(
        wordDisplayText(quizPrompt, quizScriptMode),
      )}</div>
      <div class="quiz-row" style="margin-top:0.5rem">
        <input id="quizInput" type="text" autocomplete="off" placeholder="English meaning…" />
        <button type="button" id="btnSubmitQuiz">Check</button>
      </div>
      <div id="quizOutcome"></div>
    `;

    const qLine = quizBody.querySelector<HTMLDivElement>("#quizWordLine");
    qLine?.addEventListener("click", () => {
      quizScriptMode = nextScriptMode(quizScriptMode);
      paintQuiz();
    });
    qLine?.addEventListener("keydown", (e) => {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        quizScriptMode = nextScriptMode(quizScriptMode);
        paintQuiz();
      }
    });

    quizBody.querySelector("#btnSubmitQuiz")?.addEventListener("click", async () => {
      const input = quizBody.querySelector<HTMLInputElement>("#quizInput");
      if (!quizPrompt) return;
      try {
        quizResult = await apiPost<QuizCheckResponse>("/api/quiz/check", {
          word_id: quizPrompt.word_id,
          answer: input?.value ?? "",
        });
      } catch (err) {
        quizResult = {
          correct: false,
          similarity: 0,
          expected_hint: err instanceof Error ? err.message : "Error",
        };
      }
      paintQuizOutcome();
    });

    paintQuizOutcome();
  }

  function paintQuizOutcome() {
    const out = quizBody?.querySelector("#quizOutcome");
    if (!out) return;
    if (!quizResult) {
      out.innerHTML = "";
      return;
    }
    const ok = quizResult.correct;
    out.innerHTML = `
      <p class="status ${ok ? "ok" : "bad"}">${ok ? "Nice — counted as correct." : "Not quite."}</p>
      <p class="status">Similarity score: ${quizResult.similarity.toFixed(2)}</p>
      ${
        !ok && quizResult.expected_hint
          ? `<p class="status">Hint: ${escapeHtml(quizResult.expected_hint)}</p>`
          : ""
      }
    `;
  }

  async function refreshWord() {
    const data = await apiGet<WordOfDayResponse>("/api/word-of-day");
    word = data.word;
    knownCount = typeof data.known_count === "number" ? data.known_count : 0;
    loadError = !word && data.message ? data.message : null;
  }

  async function loadQuiz() {
    quizResult = null;
    try {
      const data = await apiGet<QuizResponse>("/api/quiz");
      quizPrompt = data.prompt;
      quizMessage = data.message ?? null;
    } catch (err) {
      quizPrompt = null;
      quizMessage = err instanceof Error ? err.message : "Quiz failed.";
    }
  }

  /** If the page loaded with no known words, pick a quiz once after the first “I know this”. */
  async function ensureQuizIfStillEmpty() {
    if (quizPrompt !== null) {
      return;
    }
    await loadQuiz();
    if (quizPrompt !== null) {
      quizScriptMode = "hindi";
    }
  }

  (async () => {
    try {
      await refreshWord();
      await loadQuiz();
    } catch (err) {
      loadError = err instanceof Error ? err.message : "Failed to load.";
    }
    paintBanner();
    paintScene();
    paintQuiz();
    applyQuizVisibility();
  })();
}

mountApp();

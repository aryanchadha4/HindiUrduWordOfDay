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
type AuthMeResponse = { username: string };
type AuthSessionResponse = { token: string; username: string };

const SESSION_KEY = "hindiurdu_session";
const ANON_TOKEN_KEY = "hindiurdu_anon_token";
const LEGACY_TOKEN_KEY = "hindiurdu_user_token";
const USERNAME_KEY = "hindiurdu_username";
const QUIZ_HIDE_KEY = "hindiurdu_quiz_hidden";
const API_TIMEOUT_MS = 45_000;

function migrateLegacyToken() {
  if (localStorage.getItem(ANON_TOKEN_KEY)) return;
  const legacy = localStorage.getItem(LEGACY_TOKEN_KEY);
  if (legacy) {
    localStorage.setItem(ANON_TOKEN_KEY, legacy);
    localStorage.removeItem(LEGACY_TOKEN_KEY);
  }
}

function getOrCreateAnonToken(): string {
  migrateLegacyToken();
  let t = localStorage.getItem(ANON_TOKEN_KEY);
  if (!t) {
    t = crypto.randomUUID();
    localStorage.setItem(ANON_TOKEN_KEY, t);
  }
  return t;
}

function getSessionToken(): string | null {
  return localStorage.getItem(SESSION_KEY);
}

function apiHeaders(): Record<string, string> {
  const session = getSessionToken();
  if (session) {
    return {
      "X-User-Token": session,
      "X-Anon-Token": getOrCreateAnonToken(),
    };
  }
  return { "X-User-Token": getOrCreateAnonToken() };
}

function todayUtcLabel(): string {
  return new Date().toISOString().slice(0, 10);
}

function friendlyFetchError(err: unknown): string {
  if (err instanceof DOMException && err.name === "AbortError") {
    return "The server may be waking up. Wait a moment, then tap Retry.";
  }
  if (err instanceof TypeError) {
    return "Could not reach the server. Check your connection and tap Retry.";
  }
  return err instanceof Error ? err.message : "Request failed.";
}

function authErrorMessage(status: number, body: string): string {
  try {
    const j = JSON.parse(body) as { error?: string };
    if (j.error) return j.error;
  } catch {
    /* use fallback */
  }
  if (status === 409) return "That username is already taken.";
  if (status === 401) return "Invalid username or password.";
  return "Could not sign in. Try again.";
}

async function fetchWithTimeout(input: RequestInfo, init?: RequestInit): Promise<Response> {
  const controller = new AbortController();
  const timer = window.setTimeout(() => controller.abort(), API_TIMEOUT_MS);
  try {
    return await fetch(input, { ...init, signal: controller.signal, credentials: "include" });
  } finally {
    window.clearTimeout(timer);
  }
}

async function apiGet<T>(path: string): Promise<T> {
  const res = await fetchWithTimeout(path, { headers: apiHeaders() });
  if (!res.ok) {
    throw new Error(`${res.status} ${await res.text()}`);
  }
  return (await res.json()) as T;
}

async function apiPost<T>(path: string, body: unknown): Promise<T> {
  const res = await fetchWithTimeout(path, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      ...apiHeaders(),
    },
    body: JSON.stringify(body),
  });
  if (!res.ok) {
    throw new Error(`${res.status} ${await res.text()}`);
  }
  return (await res.json()) as T;
}

type ScriptMode = "hindi" | "roman" | "urdu";
type AuthMode = "signin" | "signup";

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
  let loading = true;
  let flipped = false;
  let scriptMode: ScriptMode = "hindi";
  let quizPrompt: QuizPrompt | null = null;
  let quizMessage: string | null = null;
  let quizScriptMode: ScriptMode = "hindi";
  let quizResult: QuizCheckResponse | null = null;
  let quizUiHidden = localStorage.getItem(QUIZ_HIDE_KEY) === "1";
  let knownCount = 0;
  let signedInUsername: string | null = localStorage.getItem(USERNAME_KEY);
  let authPanelOpen = false;
  let authMode: AuthMode = "signin";
  let authError: string | null = null;

  root.innerHTML = `
    <div class="page">
      <header>
        <h1>Hindi / Urdu word of the day</h1>
        <p>Flip the card for meaning and etymology. Tap the word to cycle Hindi → roman → Urdu.</p>
        <div class="page-meta">
          <p class="meta-line">
            <time id="wotdDate" datetime=""></time>
            <span class="meta-sep" aria-hidden="true">·</span>
            <span id="knownMeta">0</span> marked known
          </p>
          <div class="auth-bar" id="authBar"></div>
        </div>
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
  const wotdDate = root.querySelector<HTMLTimeElement>("#wotdDate");
  const knownMeta = root.querySelector<HTMLSpanElement>("#knownMeta");
  const authBar = root.querySelector<HTMLDivElement>("#authBar");

  function paintMeta() {
    const label = todayUtcLabel();
    if (wotdDate) {
      wotdDate.dateTime = label;
      wotdDate.textContent = label;
    }
    if (knownMeta) {
      knownMeta.textContent = String(knownCount);
    }
  }

  function paintAuthBar() {
    if (!authBar) return;

    if (signedInUsername) {
      authBar.innerHTML = `
        <div class="auth-signed-in">
          <span class="auth-greeting">Signed in as <strong>${escapeHtml(signedInUsername)}</strong></span>
          <button type="button" id="btnClearProgress" class="secondary meta-btn">Clear progress</button>
          <button type="button" id="btnSignOut" class="secondary meta-btn">Sign out</button>
        </div>
      `;
      authBar.querySelector("#btnSignOut")?.addEventListener("click", () => {
        void signOut();
      });
      authBar.querySelector("#btnClearProgress")?.addEventListener("click", () => {
        if (!window.confirm("Clear all words you marked as known? This cannot be undone.")) return;
        void clearProgress();
      });
      return;
    }

    authBar.innerHTML = `
      <button type="button" id="btnSaveProgress" class="meta-btn">Save progress</button>
      ${
        authPanelOpen
          ? `
        <div class="auth-panel" role="region" aria-label="Sign in or create account">
          <div class="auth-tabs">
            <button type="button" class="auth-tab ${authMode === "signin" ? "is-active" : ""}" data-mode="signin">Sign in</button>
            <button type="button" class="auth-tab ${authMode === "signup" ? "is-active" : ""}" data-mode="signup">Create account</button>
          </div>
          <form class="auth-form" id="authForm">
            <label class="auth-label">
              Username
              <input class="auth-field" id="authUsername" type="text" autocomplete="username" placeholder="letters, numbers, underscore" required minlength="3" maxlength="24" pattern="[a-z0-9_]+" />
            </label>
            <label class="auth-label">
              Password
              <input class="auth-field" id="authPassword" type="password" autocomplete="${authMode === "signup" ? "new-password" : "current-password"}" placeholder="at least 6 characters" required minlength="6" />
            </label>
            ${authError ? `<p class="auth-error" role="alert">${escapeHtml(authError)}</p>` : ""}
            <button type="submit" class="auth-submit">${authMode === "signup" ? "Create account" : "Sign in"}</button>
          </form>
        </div>
      `
          : ""
      }
    `;

    authBar.querySelector("#btnSaveProgress")?.addEventListener("click", () => {
      authPanelOpen = !authPanelOpen;
      authError = null;
      paintAuthBar();
    });

    authBar.querySelectorAll<HTMLButtonElement>(".auth-tab").forEach((tab) => {
      tab.addEventListener("click", () => {
        authMode = tab.dataset.mode === "signup" ? "signup" : "signin";
        authError = null;
        paintAuthBar();
      });
    });

    authBar.querySelector("#authForm")?.addEventListener("submit", (e) => {
      e.preventDefault();
      void submitAuth();
    });
  }

  async function submitAuth() {
    const username = authBar?.querySelector<HTMLInputElement>("#authUsername")?.value.trim() ?? "";
    const password = authBar?.querySelector<HTMLInputElement>("#authPassword")?.value ?? "";
    authError = null;
    const path = authMode === "signup" ? "/api/auth/signup" : "/api/auth/login";
    try {
      const res = await fetchWithTimeout(path, {
        method: "POST",
        headers: { "Content-Type": "application/json", ...apiHeaders() },
        body: JSON.stringify({ username, password, anon_token: getOrCreateAnonToken() }),
      });
      const text = await res.text();
      if (!res.ok) {
        authError = authErrorMessage(res.status, text);
        paintAuthBar();
        return;
      }
      const data = JSON.parse(text) as AuthSessionResponse;
      localStorage.setItem(SESSION_KEY, data.token);
      localStorage.setItem(USERNAME_KEY, data.username);
      signedInUsername = data.username;
      authPanelOpen = false;
      authError = null;
      paintAuthBar();
      await loadAll();
    } catch (err) {
      authError = friendlyFetchError(err);
      paintAuthBar();
    }
  }

  async function signOut() {
    try {
      await apiPost("/api/auth/logout", {});
    } catch {
      /* clear local session anyway */
    }
    localStorage.removeItem(SESSION_KEY);
    localStorage.removeItem(USERNAME_KEY);
    signedInUsername = null;
    authPanelOpen = false;
    paintAuthBar();
    await loadAll();
  }

  async function clearProgress() {
    try {
      await apiPost("/api/auth/clear-progress", {});
      await loadAll();
    } catch (err) {
      loadError = friendlyFetchError(err);
      paintBanner();
    }
  }

  async function restoreSession() {
    if (!getSessionToken()) {
      signedInUsername = null;
      return;
    }
    try {
      const me = await apiGet<AuthMeResponse>("/api/auth/me");
      signedInUsername = me.username;
      localStorage.setItem(USERNAME_KEY, me.username);
    } catch {
      localStorage.removeItem(SESSION_KEY);
      localStorage.removeItem(USERNAME_KEY);
      signedInUsername = null;
    }
  }

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
    if (!loadError) {
      banner.innerHTML = "";
      return;
    }
    banner.innerHTML = `
      <div class="error-banner" role="alert">
        <p>${escapeHtml(loadError)}</p>
        <button type="button" id="btnRetry" class="secondary">Retry</button>
      </div>
    `;
    banner.querySelector("#btnRetry")?.addEventListener("click", () => {
      void loadAll();
    });
  }

  function paintScene() {
    if (!scene) return;
    if (loading) {
      scene.innerHTML = `<p class="loading-state" role="status">Loading word of the day…</p>`;
      return;
    }
    if (!word) {
      scene.innerHTML = `<p class="empty-state">${
        loadError
          ? "Could not load today’s word."
          : "No word available. Mark fewer words as known, or expand the catalog."
      }</p>`;
      return;
    }
    scene.innerHTML = `
      <div class="card" id="flipCard" aria-label="Word card. Tap away from the word to flip.">
        <div class="card-inner ${flipped ? "is-flipped" : ""}" id="cardInner">
          <div class="card-face card-front">
            <span class="known-counter" aria-hidden="true">${knownCount}</span>
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
            <span class="known-counter" aria-hidden="true">${knownCount}</span>
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
        paintMeta();
        paintBanner();
        paintScene();
        await ensureQuizIfStillEmpty();
        paintQuiz();
      } catch (err) {
        loadError = friendlyFetchError(err);
        paintBanner();
      }
    });
  }

  function paintQuiz() {
    if (!quizBody) return;
    if (loading) {
      quizBody.innerHTML = `<p class="loading-state" role="status">Loading quiz…</p>`;
      return;
    }
    if (!signedInUsername && knownCount === 0 && !quizPrompt) {
      quizBody.innerHTML = `<p class="status">Mark at least one word as known on the card above to unlock the quiz. Create an account to save progress across visits.</p>`;
      return;
    }
    if (knownCount === 0 && !quizPrompt) {
      quizBody.innerHTML = `<p class="status">Mark at least one word as known on the card above to unlock the quiz.</p>`;
      return;
    }
    if (quizMessage && !quizPrompt) {
      quizBody.innerHTML = `<p class="status">${escapeHtml(quizMessage)}</p>`;
      return;
    }
    if (!quizPrompt) {
      quizBody.innerHTML = `<p class="status">No quiz word right now. Refresh after marking more words known.</p>`;
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
          expected_hint: friendlyFetchError(err),
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

  function paintAll() {
    paintMeta();
    paintAuthBar();
    paintBanner();
    paintScene();
    paintQuiz();
    applyQuizVisibility();
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
      quizMessage = friendlyFetchError(err);
    }
  }

  async function ensureQuizIfStillEmpty() {
    if (quizPrompt !== null) {
      return;
    }
    await loadQuiz();
    if (quizPrompt !== null) {
      quizScriptMode = "hindi";
    }
  }

  async function loadAll() {
    loading = true;
    loadError = null;
    paintAll();
    try {
      await refreshWord();
      await loadQuiz();
    } catch (err) {
      word = null;
      loadError = friendlyFetchError(err);
    } finally {
      loading = false;
      paintAll();
    }
  }

  (async () => {
    paintAuthBar();
    await restoreSession();
    paintAuthBar();
    await loadAll();
  })();
}

mountApp();

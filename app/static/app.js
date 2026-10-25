import { Chess, validateFen } from './vendor/chess.js';

const START_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
const FILES = 'abcdefgh';
const $ = (id) => document.getElementById(id);

const state = {
  startFen: START_FEN,
  moves: [],          // chess.js verbose moves from startFen
  ply: 0,             // how many of `moves` are on the board
  flipped: false,
  selected: null,
  analysis: null,     // { fen, bestmove, lines }
  evals: new Map(),   // fen -> eval from White's view (centipawns; mates mapped to ±10000)
  movetime: 3000,
  multipv: 3,
  auto: true,
  engine: null,       // engine id, e.g. 'stockfish'
  engines: [],        // [{ id, label }] from the server
  reqId: 0,
};
let game = new Chess(START_FEN);

loadSettings();

// ---------- helpers ----------

function gameAt(ply) {
  const g = new Chess(state.startFen);
  for (let i = 0; i < ply; i++) g.move(state.moves[i]);
  return g;
}

function squareAt(row, col) {
  const file = state.flipped ? 7 - col : col;
  const rank = state.flipped ? row : 7 - row;
  return FILES[file] + (rank + 1);
}

function squareCenter(sq) {
  const file = FILES.indexOf(sq[0]), rank = +sq[1] - 1;
  const col = state.flipped ? 7 - file : file;
  const row = state.flipped ? rank : 7 - rank;
  return [col + 0.5, row + 0.5];
}

function normalizeFen(text) {
  const parts = text.trim().split(/\s+/);
  if (parts.length === 4) parts.push('0', '1');
  if (parts.length === 5) parts.push('1');
  return parts.join(' ');
}

function formatScore(line, turn, short = false) {
  // engine scores are from the side to move; convert to White's view
  const sign = turn === 'w' ? 1 : -1;
  if (line.scoreType === 'mate') {
    const m = line.score * sign;
    return { text: (m > 0 ? '+M' : '-M') + Math.abs(m), white: m > 0 || (m === 0 && sign < 0), value: m > 0 ? 10000 : -10000 };
  }
  const cp = line.score * sign;
  const text = (cp > 0 ? '+' : '') + (cp / 100).toFixed(short ? 1 : 2);
  return { text, white: cp >= 0, value: cp };
}

function compact(n) {
  if (n >= 1e9) return (n / 1e9).toFixed(1) + 'B';
  if (n >= 1e6) return (n / 1e6).toFixed(1) + 'M';
  if (n >= 1e3) return (n / 1e3).toFixed(0) + 'k';
  return String(n);
}

function pvToSan(fen, pv, max = 14) {
  const g = new Chess(fen);
  const out = [];
  for (const uci of pv.slice(0, max)) {
    try {
      const mv = g.move({ from: uci.slice(0, 2), to: uci.slice(2, 4), promotion: uci[4] });
      out.push({ san: mv.san, color: mv.color, num: Math.ceil((g.history().length + startPlyOffset(fen)) / 2) });
    } catch { break; }
  }
  return out;
}

function startPlyOffset(fen) {
  // ply index (0-based) of the side to move in `fen`, so move numbers line up
  const [, turn, , , , full] = fen.split(' ');
  return (Number(full) - 1) * 2 + (turn === 'b' ? 1 : 0);
}

function toast(msg) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => t.classList.remove('show'), 1800);
}

// ---------- board ----------

const boardEl = $('board');
const squares = [];
for (let row = 0; row < 8; row++) {
  for (let col = 0; col < 8; col++) {
    const el = document.createElement('div');
    el.className = 'sq ' + ((row + col) % 2 ? 'dark' : 'light');
    el.dataset.row = row;
    el.dataset.col = col;
    boardEl.appendChild(el);
    squares.push(el);
  }
}

function renderBoard() {
  const last = state.ply > 0 ? state.moves[state.ply - 1] : null;
  const targets = state.selected ? game.moves({ square: state.selected, verbose: true }) : [];
  const checkSq = game.inCheck() ? findKing(game.turn()) : null;

  squares.forEach((el) => {
    const row = +el.dataset.row, col = +el.dataset.col;
    const sq = squareAt(row, col);
    el.dataset.sq = sq;
    const piece = game.get(sq);
    const target = targets.find((m) => m.to === sq);
    el.className = 'sq ' + ((row + col) % 2 ? 'dark' : 'light') +
      (last && (last.from === sq || last.to === sq) ? ' last' : '') +
      (state.selected === sq ? ' selected' : '') +
      (target ? ' target' + (target.captured ? ' capture' : '') : '') +
      (checkSq === sq ? ' check' : '');

    let html = '';
    if (col === 0) html += `<span class="coord rank">${sq[1]}</span>`;
    if (row === 7) html += `<span class="coord file">${sq[0]}</span>`;
    if (piece) html += `<img src="pieces/${piece.color}${piece.type.toUpperCase()}.svg" alt="">`;
    el.innerHTML = html;
  });
}

function findKing(color) {
  for (const row of game.board()) for (const p of row) if (p && p.type === 'k' && p.color === color) return p.square;
  return null;
}

function renderArrows() {
  const svg = $('arrows');
  svg.innerHTML = '';
  const a = state.analysis;
  if (!a || a.fen !== game.fen()) return;
  // userSpaceOnUse: a bounding-box gradient vanishes on perfectly vertical/horizontal lines
  const defs = `<defs><linearGradient id="ag" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="8" y2="8">
      <stop offset="0" stop-color="#a855f7"/><stop offset="1" stop-color="#e879f9"/></linearGradient></defs>`;
  let html = defs;
  [...a.lines].reverse().forEach((line, idx, arr) => {
    const rank = arr.length - 1 - idx; // 0 = best
    const uci = line.pv[0];
    if (!uci) return;
    html += arrow(uci.slice(0, 2), uci.slice(2, 4), rank);
  });
  svg.innerHTML = html;
}

function arrow(from, to, rank) {
  const [x1, y1] = squareCenter(from), [x2, y2] = squareCenter(to);
  const dx = x2 - x1, dy = y2 - y1, len = Math.hypot(dx, dy);
  const ux = dx / len, uy = dy / len;
  const w = rank === 0 ? 0.2 : 0.13, head = rank === 0 ? 0.42 : 0.32;
  const ex = x2 - ux * head, ey = y2 - uy * head;
  const px = -uy, py = ux;
  const color = rank === 0 ? 'url(#ag)' : 'rgba(196,181,253,0.55)';
  const opacity = rank === 0 ? 0.92 : 0.7;
  return `<g opacity="${opacity}">
    <line x1="${x1 + ux * 0.2}" y1="${y1 + uy * 0.2}" x2="${ex}" y2="${ey}" stroke="${color}" stroke-width="${w}" stroke-linecap="round"/>
    <polygon points="${x2},${y2} ${ex + px * head * 0.6},${ey + py * head * 0.6} ${ex - px * head * 0.6},${ey - py * head * 0.6}" fill="${color}" stroke="none"/>
  </g>`;
}

// ---------- interaction (click or drag) ----------

let drag = null;

boardEl.addEventListener('pointerdown', (e) => {
  const el = e.target.closest('.sq');
  if (!el) return;
  const sq = el.dataset.sq;
  const piece = game.get(sq);

  if (state.selected && state.selected !== sq && isTarget(state.selected, sq)) {
    tryMove(state.selected, sq);
    return;
  }
  if (piece && piece.color === game.turn() && !game.isGameOver()) {
    state.selected = sq;
    renderBoard();
    const img = squares.find((s) => s.dataset.sq === sq).querySelector('img');
    const size = el.getBoundingClientRect().width;
    const ghost = document.createElement('img');
    ghost.src = img.src;
    ghost.className = 'ghost';
    ghost.style.width = ghost.style.height = size * 0.9 + 'px';
    ghost.style.left = e.clientX + 'px';
    ghost.style.top = e.clientY + 'px';
    document.body.appendChild(ghost);
    img.classList.add('dragging');
    drag = { from: sq, ghost, img };
    boardEl.setPointerCapture(e.pointerId);
  } else {
    state.selected = null;
    renderBoard();
  }
});

boardEl.addEventListener('pointermove', (e) => {
  if (!drag) return;
  drag.ghost.style.left = e.clientX + 'px';
  drag.ghost.style.top = e.clientY + 'px';
});

boardEl.addEventListener('pointerup', (e) => {
  if (!drag) return;
  const { from, ghost, img } = drag;
  drag = null;
  ghost.remove();
  img.classList.remove('dragging');
  const to = squareFromPoint(e.clientX, e.clientY);
  if (to && to !== from && isTarget(from, to)) tryMove(from, to);
});

function squareFromPoint(x, y) {
  const r = boardEl.getBoundingClientRect();
  const col = Math.floor(((x - r.left) / r.width) * 8), row = Math.floor(((y - r.top) / r.height) * 8);
  if (col < 0 || col > 7 || row < 0 || row > 7) return null;
  return squareAt(row, col);
}

function isTarget(from, to) {
  return game.moves({ square: from, verbose: true }).some((m) => m.to === to);
}

async function tryMove(from, to) {
  const candidates = game.moves({ square: from, verbose: true }).filter((m) => m.to === to);
  if (!candidates.length) return;
  let promotion;
  if (candidates.some((m) => m.promotion)) promotion = await pickPromotion(game.turn());
  if (candidates[0].promotion && !promotion) { state.selected = null; renderBoard(); return; }
  applyMoves([{ from, to, promotion }]);
}

function pickPromotion(color) {
  return new Promise((resolve) => {
    const modal = $('promoModal'), box = $('promoChoices');
    box.innerHTML = '';
    for (const p of ['q', 'r', 'b', 'n']) {
      const b = document.createElement('button');
      b.innerHTML = `<img src="pieces/${color}${p.toUpperCase()}.svg" alt="${p}">`;
      b.onclick = () => { modal.hidden = true; resolve(p); };
      box.appendChild(b);
    }
    modal.onclick = (e) => { if (e.target === modal) { modal.hidden = true; resolve(null); } };
    modal.hidden = false;
  });
}

// Applies moves (SAN strings or {from,to,promotion}) at the current ply, dropping any later moves.
// All-or-nothing: returns an error message, or null on success.
function applyMoves(list) {
  const g = new Chess(game.fen());
  const played = [];
  for (let i = 0; i < list.length; i++) {
    try {
      played.push(g.move(list[i]));
    } catch {
      const label = typeof list[i] === 'string' ? list[i] : list[i].from + list[i].to;
      return `"${label}" isn't legal here (move ${i + 1} of ${list.length})`;
    }
  }
  state.moves = state.moves.slice(0, state.ply).concat(played);
  state.ply = state.moves.length;
  state.selected = null;
  update();
  return null;
}

function goTo(ply) {
  state.ply = Math.max(0, Math.min(ply, state.moves.length));
  state.selected = null;
  update();
}

function newGame(fen = START_FEN, moves = []) {
  state.startFen = fen;
  state.moves = moves;
  state.ply = moves.length;
  state.selected = null;
  state.evals.clear();
  state.analysis = null;
  update();
}

// ---------- text input ----------

const moveInput = $('moveInput');
function fitInput() {
  moveInput.style.height = 'auto';
  moveInput.style.height = moveInput.scrollHeight + 2 + 'px';
}
moveInput.addEventListener('input', fitInput);
moveInput.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) { // Enter submits, Shift+Enter adds a line
    e.preventDefault();
    $('moveForm').requestSubmit();
  }
});

$('moveForm').addEventListener('submit', (e) => {
  e.preventDefault();
  const input = moveInput;
  const text = input.value.trim();
  if (!text) return;
  const error = handleInput(text);
  const hint = $('inputHint');
  if (error) {
    hint.textContent = error;
    hint.classList.add('error');
  } else {
    hint.textContent = 'Moves are added to the current position. A pasted PGN or FEN starts a new game.';
    hint.classList.remove('error');
    input.value = '';
    fitInput();
  }
});

function handleInput(text) {
  // FEN
  if ((text.match(/\//g) || []).length === 7 && !/\d+\./.test(text)) {
    const fen = normalizeFen(text);
    const v = validateFen(fen);
    if (!v.ok) return 'Invalid FEN: ' + v.error;
    newGame(fen);
    toast('Position loaded');
    return null;
  }
  // Full PGN (headers or numbered moves starting from move 1)
  if (/^\s*\[/.test(text) || /^\s*1\.\s*\S/.test(text)) {
    const g = new Chess();
    try {
      g.loadPgn(text);
    } catch (err) {
      return 'Could not read that PGN: ' + err.message;
    }
    const header = g.header ? g.header() : {};
    const start = header.FEN ? normalizeFen(header.FEN) : START_FEN;
    newGame(start, g.history({ verbose: true }));
    toast(`Game loaded (${state.moves.length} plies)`);
    return null;
  }
  // Loose list of moves: "e4 e5 Nf3", "e2e4 e7e5", "3. Bb5 a6"
  const tokens = text
    .replace(/\{[^}]*\}|\([^)]*\)|\$\d+/g, ' ')
    .split(/\s+/)
    .map((t) => t.replace(/^\d+\.+/, ''))
    .filter((t) => t && !/^(1-0|0-1|1\/2-1\/2|\*)$/.test(t));
  if (!tokens.length) return 'No moves found';
  const list = tokens.map((t) =>
    /^[a-h][1-8][a-h][1-8][qrbn]?$/i.test(t)
      ? { from: t.slice(0, 2).toLowerCase(), to: t.slice(2, 4).toLowerCase(), promotion: t[4]?.toLowerCase() }
      : t.replace(/[!?]+$/, '').replace(/^0-0-0$/, 'O-O-O').replace(/^0-0$/, 'O-O'));
  return applyMoves(list);
}

// ---------- analysis ----------

let analyzeTimer = null;

function scheduleAnalysis(force = false) {
  clearTimeout(analyzeTimer);
  if (!state.auto && !force) return;
  analyzeTimer = setTimeout(analyze, force ? 0 : 200);
}

async function analyze() {
  const fen = game.fen();
  if (game.isGameOver()) { renderAnalysis(); return; }
  if (!state.engine) return; // engine list not loaded yet
  if (state.analysis && state.analysis.fen === fen && state.analysis.engineId === state.engine &&
      state.analysis.movetime >= state.movetime && state.analysis.lines.length >= state.multipv) { renderAnalysis(); return; }
  const id = ++state.reqId;
  $('thinking').classList.add('on');
  try {
    const res = await fetch('/api/analyze', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ fen, movetime: state.movetime, multipv: state.multipv, engine: state.engine }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || res.statusText);
    setEngineStatus(true, data.engine);
    if (id !== state.reqId) return; // a newer request superseded this one
    state.analysis = { fen, movetime: state.movetime, engineId: state.engine, ...data };
    if (data.lines.length) state.evals.set(fen, formatScore(data.lines[0], fen.split(' ')[1]).value);
  } catch (err) {
    if (id !== state.reqId) return;
    setEngineStatus(false);
    toast('Engine error: ' + err.message);
  } finally {
    if (id === state.reqId) $('thinking').classList.remove('on');
  }
  if (id === state.reqId) { renderAnalysis(); renderArrows(); renderGraph(); }
}

function setEngineStatus(online, label) {
  const pill = $('engineStatus');
  pill.classList.toggle('online', online);
  pill.classList.toggle('offline', !online);
  pill.querySelector('.label').textContent = online ? `${label || 'Engine'} online` : 'Engine offline';
}

async function loadEngines() {
  try {
    const res = await fetch('/api/engines');
    const data = await res.json();
    state.engines = data.engines;
    if (!state.engines.some((e) => e.id === state.engine)) state.engine = data.default;
  } catch {
    setEngineStatus(false);
    return;
  }
  const seg = $('engineSeg');
  seg.innerHTML = '';
  for (const e of state.engines) {
    const b = document.createElement('button');
    b.textContent = e.label;
    b.classList.toggle('active', e.id === state.engine);
    b.addEventListener('click', () => {
      state.engine = e.id;
      seg.querySelectorAll('button').forEach((x) => x.classList.toggle('active', x === b));
      saveSettings();
      scheduleAnalysis(true);
    });
    seg.appendChild(b);
  }
  const current = state.engines.find((e) => e.id === state.engine);
  setEngineStatus(true, current && current.label);
  scheduleAnalysis(true);
}

function gameOverText() {
  if (game.isCheckmate()) return { big: '#', sub: `Checkmate — ${game.turn() === 'w' ? 'Black' : 'White'} wins` };
  if (game.isStalemate()) return { big: '½', sub: 'Stalemate — draw' };
  if (game.isThreefoldRepetition()) return { big: '½', sub: 'Threefold repetition — draw' };
  if (game.isInsufficientMaterial()) return { big: '½', sub: 'Insufficient material — draw' };
  if (game.isDraw()) return { big: '½', sub: 'Draw (50-move rule)' };
  return null;
}

function renderAnalysis() {
  const fen = game.fen(), turn = game.turn();
  const a = state.analysis && state.analysis.fen === fen ? state.analysis : null;
  const over = gameOverText();
  const best = $('bestMove'), sub = $('bestSub'), evalBig = $('evalBig'), play = $('playBestBtn');

  if (over) {
    best.textContent = over.big;
    sub.textContent = over.sub;
    play.disabled = true;
    const mateVal = game.isCheckmate() ? (turn === 'w' ? -10000 : 10000) : 0;
    setEval(mateVal, game.isCheckmate() ? (turn === 'w' ? '0-1' : '1-0') : '½-½');
    $('lines').innerHTML = '<div class="empty">Game over</div>';
    return;
  }
  if (!a || !a.lines.length) {
    play.disabled = true;
    best.textContent = '…';
    sub.textContent = state.auto ? 'Analyzing this position' : 'Press "Analyze now"';
    $('lines').innerHTML = `<div class="empty">${state.auto ? 'Analyzing…' : 'Not analyzed yet'}</div>`;
    return;
  }

  const top = a.lines[0];
  const san = pvToSan(fen, top.pv, 1)[0];
  best.textContent = san ? san.san : top.pv[0];
  sub.textContent = `${turn === 'w' ? 'White' : 'Black'} to play · ${top.pv[0].slice(0, 2)} → ${top.pv[0].slice(2, 4)}`;
  play.disabled = false;

  const s = formatScore(top, turn);
  setEval(s.value, s.text);
  $('statDepth').textContent = top.depth;
  $('statNodes').textContent = compact(top.nodes);
  $('statNps').textContent = compact(top.nps) + '/s';

  $('lines').innerHTML = '';
  a.lines.forEach((line) => {
    const sc = formatScore(line, turn, true);
    const moves = pvToSan(fen, line.pv);
    let text = '';
    moves.forEach((m, i) => {
      if (m.color === 'w') text += `${m.num}. `;
      else if (i === 0) text += `${m.num}… `;
      text += i === 0 ? `<b>${m.san}</b> ` : `${m.san} `;
    });
    const el = document.createElement('button');
    el.className = 'line';
    el.innerHTML = `<span class="score ${sc.white ? 'w' : 'b'}">${sc.text}</span><span class="moves">${text}</span>`;
    el.title = 'Play this move';
    el.onclick = () => applyMoves([{ from: line.pv[0].slice(0, 2), to: line.pv[0].slice(2, 4), promotion: line.pv[0][4] }]);
    $('lines').appendChild(el);
  });
}

function setEval(value, label) {
  const pct = 50 + 50 * (2 / (1 + Math.exp(-0.004 * value)) - 1);
  $('evalFill').style.height = pct + '%';
  const bar = $('evalBar');
  bar.classList.toggle('black-ahead', value < 0);
  $('evalBarLabel').textContent = label.replace('+', '');
  const big = $('evalBig');
  big.textContent = label;
  big.classList.toggle('pos', value >= 0);
  big.classList.toggle('neg', value < 0);
}

$('playBestBtn').addEventListener('click', () => {
  const a = state.analysis;
  if (!a || a.fen !== game.fen() || !a.bestmove) return;
  const u = a.bestmove;
  applyMoves([{ from: u.slice(0, 2), to: u.slice(2, 4), promotion: u[4] }]);
});

// ---------- eval graph ----------

function renderGraph() {
  const svg = $('graph');
  const W = 300, H = 90, n = state.moves.length;
  const fens = [];
  const g = new Chess(state.startFen);
  fens.push(g.fen());
  for (const m of state.moves) { g.move(m); fens.push(g.fen()); }

  const y = (v) => H / 2 - (H / 2 - 4) * (2 / (1 + Math.exp(-0.004 * v)) - 1);
  const x = (i) => (n === 0 ? W / 2 : (i / n) * W);
  let pts = [];
  fens.forEach((f, i) => { if (state.evals.has(f)) pts.push([x(i), y(state.evals.get(f))]); });

  let html = `<defs>
    <linearGradient id="gfill" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#d946ef" stop-opacity="0.45"/><stop offset="1" stop-color="#7c3aed" stop-opacity="0.02"/>
    </linearGradient></defs>
    <line x1="0" y1="${H / 2}" x2="${W}" y2="${H / 2}" stroke="rgba(167,128,255,0.25)" stroke-width="1" stroke-dasharray="3 4"/>`;
  if (pts.length > 1) {
    const d = pts.map((p, i) => (i ? 'L' : 'M') + p[0].toFixed(1) + ' ' + p[1].toFixed(1)).join(' ');
    html += `<path d="${d} L${pts.at(-1)[0]} ${H} L${pts[0][0]} ${H} Z" fill="url(#gfill)" stroke="none"/>`;
    html += `<path d="${d}" stroke="#c084fc" stroke-width="2" fill="none" vector-effect="non-scaling-stroke"/>`;
  }
  const cx = x(state.ply);
  html += `<line x1="${cx}" y1="0" x2="${cx}" y2="${H}" stroke="rgba(240,171,252,0.6)" stroke-width="1" vector-effect="non-scaling-stroke"/>`;
  if (state.evals.has(fens[state.ply])) {
    html += `<circle cx="${cx}" cy="${y(state.evals.get(fens[state.ply]))}" r="3.5" fill="#f0abfc" stroke="none"/>`;
  }
  svg.innerHTML = html;
  $('graphLabel').textContent = pts.length ? `${pts.length} position${pts.length > 1 ? 's' : ''} analyzed` : 'Analyzed positions appear here';
}

$('graph').addEventListener('click', (e) => {
  const r = e.currentTarget.getBoundingClientRect();
  if (!state.moves.length) return;
  goTo(Math.round(((e.clientX - r.left) / r.width) * state.moves.length));
});

// ---------- move list ----------

function renderMoveList() {
  const box = $('moveList');
  if (!state.moves.length) { box.innerHTML = '<div class="empty">No moves yet</div>'; return; }
  const offset = startPlyOffset(state.startFen);
  let html = '';
  state.moves.forEach((m, i) => {
    const abs = offset + i;
    if (abs % 2 === 0 || i === 0) {
      html += `<div class="num">${Math.floor(abs / 2) + 1}.</div>`;
      if (abs % 2 === 1) html += '<div class="mv">…</div>';
    }
    html += `<div class="mv${i + 1 === state.ply ? ' current' : ''}" data-ply="${i + 1}">${m.san}</div>`;
  });
  box.innerHTML = html;
  // scroll only the list, never the page
  const cur = box.querySelector('.current');
  if (cur) {
    const top = cur.offsetTop; // .movelist is position:relative, so this is relative to the list
    if (top < box.scrollTop) box.scrollTop = top;
    else if (top + cur.offsetHeight > box.scrollTop + box.clientHeight) box.scrollTop = top + cur.offsetHeight - box.clientHeight;
  }
}

$('moveList').addEventListener('click', (e) => {
  const el = e.target.closest('[data-ply]');
  if (el) goTo(+el.dataset.ply);
});

// ---------- controls ----------

document.querySelectorAll('[data-nav]').forEach((b) =>
  b.addEventListener('click', () => {
    const nav = b.dataset.nav;
    goTo(nav === 'first' ? 0 : nav === 'prev' ? state.ply - 1 : nav === 'next' ? state.ply + 1 : state.moves.length);
  }));

$('undoBtn').addEventListener('click', () => {
  if (!state.moves.length) return;
  state.moves.pop();
  goTo(state.moves.length);
});
$('flipBtn').addEventListener('click', flip);
$('resetBtn').addEventListener('click', () => newGame());
$('analyzeBtn').addEventListener('click', () => { state.analysis = null; scheduleAnalysis(true); });
$('copyFenBtn').addEventListener('click', async () => {
  try { await navigator.clipboard.writeText(game.fen()); toast('FEN copied'); } catch { toast(game.fen()); }
});

function flip() {
  state.flipped = !state.flipped;
  $('evalBar').classList.toggle('flipped', state.flipped);
  renderBoard();
  renderArrows();
}

document.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
  if (e.key === 'ArrowLeft') goTo(state.ply - 1);
  else if (e.key === 'ArrowRight') goTo(state.ply + 1);
  else if (e.key === 'Home') goTo(0);
  else if (e.key === 'End') goTo(state.moves.length);
  else if (e.key.toLowerCase() === 'f') flip();
});

document.querySelectorAll('.segmented').forEach((seg) => {
  const key = seg.dataset.setting;
  seg.querySelectorAll('button').forEach((b) => {
    b.classList.toggle('active', +b.dataset.value === state[key]);
    b.addEventListener('click', () => {
      state[key] = +b.dataset.value;
      seg.querySelectorAll('button').forEach((x) => x.classList.toggle('active', x === b));
      saveSettings();
      scheduleAnalysis(true);
    });
  });
});

const autoToggle = $('autoToggle');
autoToggle.setAttribute('aria-checked', String(state.auto));
autoToggle.addEventListener('click', () => {
  state.auto = !state.auto;
  autoToggle.setAttribute('aria-checked', String(state.auto));
  saveSettings();
  if (state.auto) scheduleAnalysis(true);
});

function loadSettings() {
  try {
    const s = JSON.parse(localStorage.getItem('chessai-settings') || '{}');
    if ([1000, 3000, 10000].includes(s.movetime)) state.movetime = s.movetime;
    if ([1, 3, 5].includes(s.multipv)) state.multipv = s.multipv;
    if (typeof s.auto === 'boolean') state.auto = s.auto;
    if (typeof s.engine === 'string') state.engine = s.engine;
  } catch { /* storage unavailable */ }
}

function saveSettings() {
  try {
    localStorage.setItem('chessai-settings', JSON.stringify({ movetime: state.movetime, multipv: state.multipv, auto: state.auto, engine: state.engine }));
  } catch { /* storage unavailable */ }
}

// ---------- main update ----------

function update() {
  game = gameAt(state.ply);
  const turn = game.turn();
  const label = $('turnLabel');
  label.style.setProperty('--turn-color', turn === 'w' ? '#ffffff' : '#120c22');
  label.textContent = game.isCheckmate() ? 'Checkmate' : `${turn === 'w' ? 'White' : 'Black'} to move${game.inCheck() ? ' · Check' : ''}`;
  label.classList.toggle('alert', game.inCheck());
  renderBoard();
  renderArrows();
  renderMoveList();
  renderGraph();
  renderAnalysis();
  scheduleAnalysis();
}

update();
loadEngines();

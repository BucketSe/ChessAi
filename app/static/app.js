import { Chess, validateFen } from './vendor/chess.js';

const APP_VERSION = '1.1.0';
const START_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
const FILES = 'abcdefgh';
const $ = (id) => document.getElementById(id);

const THEMES = [
  { id: 'amethyst', name: 'Amethyst', bg: '#07050d', panel: '#1a1330', a: '#7c3aed', b: '#d946ef', text: '#ece8f7' },
  { id: 'midnight', name: 'Midnight', bg: '#050811', panel: '#111a30', a: '#2563eb', b: '#22d3ee', text: '#e6edfb' },
  { id: 'emerald', name: 'Emerald', bg: '#04090a', panel: '#0f2420', a: '#059669', b: '#84cc16', text: '#e3f6ef' },
  { id: 'sunset', name: 'Sunset', bg: '#0c0507', panel: '#2a1018', a: '#e11d48', b: '#fb923c', text: '#fbe9ec' },
  { id: 'light', name: 'Light', bg: '#f4f1fb', panel: '#ffffff', a: '#7c3aed', b: '#d946ef', text: '#1d1530' },
];
const BOARDS = [
  { id: 'amethyst', name: 'Amethyst', light: '#b9aae6', dark: '#6d59b2' },
  { id: 'classic', name: 'Classic', light: '#eeeed2', dark: '#769656' },
  { id: 'wood', name: 'Wood', light: '#f0d9b5', dark: '#b58863' },
  { id: 'ice', name: 'Ice', light: '#dee3e6', dark: '#8ca2ad' },
  { id: 'graphite', name: 'Graphite', light: '#a3a7b0', dark: '#5b616c' },
];
const PIECE_SETS = [
  { id: 'cburnett', name: 'Classic' },
  { id: 'merida', name: 'Merida' },
  { id: 'chessnut', name: 'Chessnut' },
  { id: 'spatial', name: 'Spatial' },
  { id: 'fantasy', name: 'Fantasy' },
  { id: 'mpchess', name: 'MPChess' },
];

const DEFAULT_SETTINGS = {
  theme: 'amethyst', board: 'amethyst', pieces: 'cburnett',
  coords: true, arrows: true, evalbar: true, lastMove: true, legalDots: true, graph: true,
  engine: null, movetime: 3000, multipv: 3, auto: true, threads: 0, hash: 256,
};
const settings = { ...DEFAULT_SETTINGS };

const state = {
  startFen: START_FEN,
  moves: [],          // chess.js verbose moves from startFen
  ply: 0,             // how many of `moves` are on the board
  flipped: false,
  selected: null,
  analysis: null,     // { fen, bestmove, lines, ... }
  evals: new Map(),   // fen -> eval from White's view (centipawns; mates mapped to ±10000)
  engines: [],        // [{ id, label }] from the server
  cpus: 4,
  reqId: 0,
};
let game = new Chess(START_FEN);

const editor = {
  active: false,
  board: {},          // square -> piece code like 'wK'
  side: 'w',
  castling: { K: true, Q: true, k: true, q: true },
  tool: 'hand',       // 'hand' | 'erase' | piece code
  paint: null,        // active paint action while the pointer is down
};

// ---------- helpers ----------

const pieceSrc = (color, type) => `pieces/${settings.pieces}/${color}${type.toUpperCase()}.svg`;

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

function startPlyOffset(fen) {
  // ply index (0-based) of the side to move in `fen`, so move numbers line up
  const [, turn, , , , full] = fen.split(' ');
  return (Number(full) - 1) * 2 + (turn === 'b' ? 1 : 0);
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
    el.dataset.row = row;
    el.dataset.col = col;
    boardEl.appendChild(el);
    squares.push(el);
  }
}

function pieceAt(sq) {
  if (editor.active) {
    const code = editor.board[sq];
    return code ? { color: code[0], type: code[1].toLowerCase() } : null;
  }
  return game.get(sq) || null;
}

function renderBoard() {
  const live = !editor.active;
  const last = live && state.ply > 0 ? state.moves[state.ply - 1] : null;
  const targets = live && state.selected ? game.moves({ square: state.selected, verbose: true }) : [];
  const checkSq = live && game.inCheck() ? findKing(game.turn()) : null;

  for (const el of squares) {
    const row = +el.dataset.row, col = +el.dataset.col;
    const sq = squareAt(row, col);
    el.dataset.sq = sq;
    const piece = pieceAt(sq);
    const target = targets.find((m) => m.to === sq);
    el.className = 'sq ' + ((row + col) % 2 ? 'dark' : 'light') +
      (last && (last.from === sq || last.to === sq) ? ' last' : '') +
      (live && state.selected === sq ? ' selected' : '') +
      (target ? ' target' + (target.captured ? ' capture' : '') : '') +
      (checkSq === sq ? ' check' : '');

    let html = '';
    if (col === 0) html += `<span class="coord rank">${sq[1]}</span>`;
    if (row === 7) html += `<span class="coord file">${sq[0]}</span>`;
    if (piece) html += `<img src="${pieceSrc(piece.color, piece.type)}" alt="">`;
    el.innerHTML = html;
  }
}

function findKing(color) {
  for (const row of game.board()) for (const p of row) if (p && p.type === 'k' && p.color === color) return p.square;
  return null;
}

function renderArrows() {
  const svg = $('arrows');
  svg.innerHTML = '';
  const a = state.analysis;
  if (editor.active || !a || a.fen !== game.fen()) return;
  // userSpaceOnUse: a bounding-box gradient vanishes on perfectly vertical/horizontal lines
  // (CSS variables only work through style=, not SVG presentation attributes)
  let html = `<defs><linearGradient id="ag" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="8" y2="8">
      <stop offset="0" style="stop-color:var(--accent)"/><stop offset="1" style="stop-color:var(--accent-2)"/></linearGradient></defs>`;
  a.lines.slice(0, settings.multipv).reverse().forEach((line, idx, arr) => {
    const uci = line.pv[0];
    if (uci) html += arrow(uci.slice(0, 2), uci.slice(2, 4), arr.length - 1 - idx);
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
  const color = rank === 0 ? 'url(#ag)' : 'var(--accent)';
  const opacity = rank === 0 ? 0.92 : 0.45;
  return `<g opacity="${opacity}">
    <line x1="${x1 + ux * 0.2}" y1="${y1 + uy * 0.2}" x2="${ex}" y2="${ey}" style="stroke:${color}" stroke-width="${w}" stroke-linecap="round"/>
    <polygon points="${x2},${y2} ${ex + px * head * 0.6},${ey + py * head * 0.6} ${ex - px * head * 0.6},${ey - py * head * 0.6}" style="fill:${color};stroke:none"/>
  </g>`;
}

// ---------- pointer interaction ----------

let drag = null;

function startGhost(e, sq, size) {
  const el = squares.find((s) => s.dataset.sq === sq);
  const img = el.querySelector('img');
  const ghost = document.createElement('img');
  ghost.src = img.src;
  ghost.className = 'ghost';
  ghost.style.width = ghost.style.height = size * 0.9 + 'px';
  ghost.style.left = e.clientX + 'px';
  ghost.style.top = e.clientY + 'px';
  document.body.appendChild(ghost);
  img.classList.add('dragging');
  return { from: sq, ghost, img };
}

boardEl.addEventListener('contextmenu', (e) => e.preventDefault());

boardEl.addEventListener('pointerdown', (e) => {
  const el = e.target.closest('.sq');
  if (!el) return;
  const sq = el.dataset.sq;
  boardEl.setPointerCapture(e.pointerId);
  if (editor.active) return editorPointerDown(e, sq, el);

  const piece = game.get(sq);
  if (state.selected && state.selected !== sq && isTarget(state.selected, sq)) {
    tryMove(state.selected, sq);
    return;
  }
  if (piece && piece.color === game.turn() && !game.isGameOver()) {
    state.selected = sq;
    renderBoard();
    drag = startGhost(e, sq, el.getBoundingClientRect().width);
  } else {
    state.selected = null;
    renderBoard();
  }
});

boardEl.addEventListener('pointermove', (e) => {
  if (drag) {
    drag.ghost.style.left = e.clientX + 'px';
    drag.ghost.style.top = e.clientY + 'px';
  } else if (editor.active && editor.paint) {
    const sq = squareFromPoint(e.clientX, e.clientY);
    if (sq) editorApply(sq, editor.paint);
  }
});

boardEl.addEventListener('pointerup', (e) => {
  editor.paint = null;
  if (!drag) return;
  const { from, ghost, img } = drag;
  drag = null;
  ghost.remove();
  img.classList.remove('dragging');
  const to = squareFromPoint(e.clientX, e.clientY);
  if (editor.active) {
    const code = editor.board[from];
    delete editor.board[from];
    if (to) editorPlace(to, code); // dropped off the board = removed
    renderEditor();
  } else if (to && to !== from && isTarget(from, to)) {
    tryMove(from, to);
  }
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
  if (candidates.some((m) => m.promotion)) {
    promotion = await pickPromotion(game.turn());
    if (!promotion) { state.selected = null; renderBoard(); return; }
  }
  applyMoves([{ from, to, promotion }]);
}

function pickPromotion(color) {
  return new Promise((resolve) => {
    const modal = $('promoModal'), box = $('promoChoices');
    box.innerHTML = '';
    for (const p of ['q', 'r', 'b', 'n']) {
      const b = document.createElement('button');
      b.innerHTML = `<img src="${pieceSrc(color, p)}" alt="${p}">`;
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
  if (editor.active) return;
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

// ---------- board editor ----------

const HOME = { K: ['e1', 'h1'], Q: ['e1', 'a1'], k: ['e8', 'h8'], q: ['e8', 'a8'] };
const CASTLE_PIECES = { K: ['wK', 'wR'], Q: ['wK', 'wR'], k: ['bK', 'bR'], q: ['bK', 'bR'] };

function buildPalette() {
  const pal = $('palette');
  const tools = [
    { id: 'hand', html: '<svg viewBox="0 0 24 24"><path d="M18 11V6a2 2 0 0 0-4 0v5M14 10V4a2 2 0 0 0-4 0v6M10 10.5V6a2 2 0 0 0-4 0v8l-1.5-1.5a2 2 0 0 0-3 2.6L6 20a6 6 0 0 0 5 2h2a7 7 0 0 0 7-7v-4a2 2 0 0 0-4 0"/></svg>', title: 'Move pieces' },
    ...['K', 'Q', 'R', 'B', 'N', 'P'].map((t) => ({ id: 'w' + t, html: `<img src="${pieceSrc('w', t)}" alt="">`, title: 'White ' + t })),
    { id: 'erase', html: '<svg viewBox="0 0 24 24"><path d="M3 6h18M8 6V4h8v2M6 6l1 14h10l1-14"/></svg>', title: 'Erase' },
    ...['K', 'Q', 'R', 'B', 'N', 'P'].map((t) => ({ id: 'b' + t, html: `<img src="${pieceSrc('b', t)}" alt="">`, title: 'Black ' + t })),
  ];
  pal.innerHTML = '';
  for (const t of tools) {
    const b = document.createElement('button');
    b.innerHTML = t.html;
    b.title = t.title;
    b.dataset.tool = t.id;
    b.classList.toggle('active', editor.tool === t.id);
    b.onclick = () => {
      editor.tool = t.id;
      pal.querySelectorAll('button').forEach((x) => x.classList.toggle('active', x === b));
    };
    pal.appendChild(b);
  }
}

function openEditor() {
  editor.board = {};
  for (const row of game.board()) for (const p of row) if (p) editor.board[p.square] = p.color + p.type.toUpperCase();
  editor.side = game.turn();
  const rights = game.fen().split(' ')[2];
  for (const k of 'KQkq') editor.castling[k] = rights.includes(k);
  editor.active = true;
  state.selected = null;
  document.body.classList.add('editing-mode');
  $('editBtn').classList.add('active');
  $('editor').hidden = false;
  $('nav').hidden = true;
  setEditorHint();
  buildPalette();
  renderEditor();
}

function closeEditor(refresh = true) {
  editor.active = false;
  editor.paint = null;
  document.body.classList.remove('editing-mode');
  $('editBtn').classList.remove('active');
  $('editor').hidden = true;
  $('nav').hidden = false;
  if (refresh) update();
}

function editorPlace(sq, code) {
  if (!code) return;
  if (code[1] === 'K') { // only one king per side: move it
    for (const [s, c] of Object.entries(editor.board)) if (c === code) delete editor.board[s];
  }
  editor.board[sq] = code;
}

function editorApply(sq, action) {
  if (action === 'erase') delete editor.board[sq];
  else editorPlace(sq, action);
  renderEditor();
}

function editorPointerDown(e, sq, el) {
  if (e.button === 2) { editor.paint = 'erase'; editorApply(sq, 'erase'); return; }
  if (editor.tool === 'hand') {
    if (editor.board[sq]) drag = startGhost(e, sq, el.getBoundingClientRect().width);
    return;
  }
  if (editor.tool === 'erase') { editor.paint = 'erase'; editorApply(sq, 'erase'); return; }
  if (editor.board[sq] === editor.tool) { editor.paint = 'erase'; editorApply(sq, 'erase'); return; } // click again to remove
  editor.paint = editor.tool;
  editorApply(sq, editor.tool);
}

function castleAvailable(k) {
  const [kingSq, rookSq] = HOME[k], [king, rook] = CASTLE_PIECES[k];
  return editor.board[kingSq] === king && editor.board[rookSq] === rook;
}

function editorFen() {
  let placement = '';
  for (let rank = 7; rank >= 0; rank--) {
    let empty = 0;
    for (let file = 0; file < 8; file++) {
      const code = editor.board[FILES[file] + (rank + 1)];
      if (!code) { empty++; continue; }
      if (empty) placement += empty, empty = 0;
      placement += code[0] === 'w' ? code[1] : code[1].toLowerCase();
    }
    if (empty) placement += empty;
    if (rank) placement += '/';
  }
  const castle = [...'KQkq'].filter((k) => editor.castling[k] && castleAvailable(k)).join('') || '-';
  return `${placement} ${editor.side} ${castle} - 0 1`;
}

function validateEditor(fen) {
  const codes = Object.values(editor.board);
  const count = (c) => codes.filter((x) => x === c).length;
  if (count('wK') !== 1 || count('bK') !== 1) return 'Each side needs exactly one king.';
  if (count('wP') > 8 || count('bP') > 8) return 'A side can have at most 8 pawns.';
  if (Object.entries(editor.board).some(([sq, c]) => c[1] === 'P' && (sq[1] === '1' || sq[1] === '8')))
    return 'Pawns can\'t stand on the first or last rank.';
  const v = validateFen(fen);
  if (!v.ok) return v.error;
  const parts = fen.split(' ');
  parts[1] = editor.side === 'w' ? 'b' : 'w';
  try {
    if (new Chess(parts.join(' ')).inCheck()) {
      const other = editor.side === 'w' ? 'Black' : 'White';
      return `${other} is in check, so it can't be ${editor.side === 'w' ? 'White' : 'Black'}'s turn.`;
    }
  } catch { /* chess.js rejected it; validateFen above already passed */ }
  return null;
}

function setEditorHint(error) {
  const hint = $('editHint');
  hint.classList.toggle('error', !!error);
  hint.textContent = error || 'Pick a piece, then click or drag across squares to place it. Right-click erases. With the hand tool, drag pieces around or off the board.';
}

function renderEditor() {
  renderBoard();
  $('arrows').innerHTML = '';
  $('turnLabel').textContent = 'Board editor';
  $('turnLabel').classList.add('editing');
  $('turnLabel').classList.remove('alert');
  $('editSide').querySelectorAll('button').forEach((b) => b.classList.toggle('active', b.dataset.value === editor.side));
  $('editCastle').querySelectorAll('button').forEach((b) => {
    const k = b.dataset.castle, ok = castleAvailable(k);
    b.disabled = !ok;
    b.classList.toggle('on', ok && editor.castling[k]);
  });
}

$('editBtn').addEventListener('click', () => (editor.active ? closeEditor() : openEditor()));
$('editCancel').addEventListener('click', closeEditor);
$('editSide').addEventListener('click', (e) => {
  const b = e.target.closest('button');
  if (b) { editor.side = b.dataset.value; renderEditor(); }
});
$('editCastle').addEventListener('click', (e) => {
  const b = e.target.closest('button');
  if (b && !b.disabled) { editor.castling[b.dataset.castle] = !editor.castling[b.dataset.castle]; renderEditor(); }
});
$('editStart').addEventListener('click', () => {
  const g = new Chess(START_FEN);
  editor.board = {};
  for (const row of g.board()) for (const p of row) if (p) editor.board[p.square] = p.color + p.type.toUpperCase();
  editor.side = 'w';
  for (const k of 'KQkq') editor.castling[k] = true;
  setEditorHint();
  renderEditor();
});
$('editClear').addEventListener('click', () => {
  editor.board = { e1: 'wK', e8: 'bK' }; // kings are required, so keep them
  setEditorHint();
  renderEditor();
});
$('editDone').addEventListener('click', () => {
  const fen = editorFen();
  const error = validateEditor(fen);
  if (error) { setEditorHint(error); return; }
  closeEditor(false);
  newGame(fen);
  toast('Position set up');
});

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
  const text = moveInput.value.trim();
  if (!text) return;
  if (editor.active) closeEditor();
  const error = handleInput(text);
  const hint = $('inputHint');
  if (error) {
    hint.textContent = error;
    hint.classList.add('error');
  } else {
    hint.textContent = 'Moves are added to the current position. A pasted PGN or FEN starts a new game.';
    hint.classList.remove('error');
    moveInput.value = '';
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
  if (!settings.auto && !force) return;
  analyzeTimer = setTimeout(analyze, force ? 0 : 200);
}

async function analyze() {
  if (editor.active) return;
  const fen = game.fen();
  if (game.isGameOver()) { renderAnalysis(); return; }
  if (!settings.engine) return; // engine list not loaded yet
  const a = state.analysis;
  if (a && a.fen === fen && a.engineId === settings.engine && a.movetime >= settings.movetime &&
      a.lines.length >= settings.multipv) { renderAnalysis(); return; }
  const id = ++state.reqId;
  $('thinking').classList.add('on');
  try {
    const res = await fetch('/api/analyze', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        fen, movetime: settings.movetime, multipv: settings.multipv, engine: settings.engine,
        threads: settings.threads, hash: settings.hash,
      }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || res.statusText);
    setEngineStatus(true, data.engine);
    if (id !== state.reqId) return; // a newer request superseded this one
    state.analysis = { fen, movetime: settings.movetime, engineId: settings.engine, ...data };
    if (data.lines.length) state.evals.set(fen, formatScore(data.lines[0], fen.split(' ')[1]).value);
  } catch (err) {
    if (id !== state.reqId) return;
    setEngineStatus(false);
    toast('Engine error: ' + err.message);
  } finally {
    if (id === state.reqId) $('thinking').classList.remove('on');
  }
  if (id === state.reqId && !editor.active) { renderAnalysis(); renderArrows(); renderGraph(); }
}

function setEngineStatus(online, label) {
  const pill = $('engineStatus');
  pill.classList.toggle('online', online);
  pill.classList.toggle('offline', !online);
  pill.querySelector('.label').textContent = online ? `${label || 'Engine'} online` : 'Engine offline';
}

async function loadEngines() {
  try {
    const data = await (await fetch('/api/engines')).json();
    state.engines = data.engines;
    state.cpus = data.cpus || 4;
    if (!state.engines.some((e) => e.id === settings.engine)) settings.engine = data.default;
    $('threadsHelp').textContent = `More threads = deeper analysis (Stockfish only). Auto uses ${data.defaultThreads} of ${state.cpus}.`;
  } catch {
    setEngineStatus(false);
    return;
  }
  document.querySelectorAll('.engine-seg').forEach((seg) => {
    seg.innerHTML = state.engines.map((e) => `<button data-value="${e.id}">${e.label}</button>`).join('');
  });
  const threadOpts = [0, 1, 2, 4, 8, 16, 32].filter((n) => n <= state.cpus);
  $('threadsSeg').innerHTML = threadOpts.map((n) => `<button data-value="${n}">${n || 'Auto'}</button>`).join('');
  syncControls();
  const current = state.engines.find((e) => e.id === settings.engine);
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
  const best = $('bestMove'), sub = $('bestSub'), play = $('playBestBtn');

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
    sub.textContent = settings.auto ? 'Analyzing this position' : 'Press "Analyze now"';
    $('lines').innerHTML = `<div class="empty">${settings.auto ? 'Analyzing…' : 'Not analyzed yet'}</div>`;
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
  a.lines.slice(0, settings.multipv).forEach((line) => {
    const sc = formatScore(line, turn, true);
    let text = '';
    pvToSan(fen, line.pv).forEach((m, i) => {
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
  $('evalBar').classList.toggle('black-ahead', value < 0);
  $('evalBarLabel').textContent = label.replace('+', '');
  $('evalBig').textContent = label;
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
  const pts = [];
  fens.forEach((f, i) => { if (state.evals.has(f)) pts.push([x(i), y(state.evals.get(f))]); });

  let html = `<defs>
    <linearGradient id="gfill" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" style="stop-color:var(--accent-2);stop-opacity:0.45"/><stop offset="1" style="stop-color:var(--accent);stop-opacity:0.02"/>
    </linearGradient></defs>
    <line x1="0" y1="${H / 2}" x2="${W}" y2="${H / 2}" style="stroke:var(--border-strong)" stroke-width="1" stroke-dasharray="3 4"/>`;
  if (pts.length > 1) {
    const d = pts.map((p, i) => (i ? 'L' : 'M') + p[0].toFixed(1) + ' ' + p[1].toFixed(1)).join(' ');
    html += `<path d="${d} L${pts.at(-1)[0]} ${H} L${pts[0][0]} ${H} Z" style="fill:url(#gfill);stroke:none"/>`;
    html += `<path d="${d}" style="stroke:var(--accent-2);fill:none" stroke-width="2" vector-effect="non-scaling-stroke"/>`;
  }
  const cx = x(state.ply);
  html += `<line x1="${cx}" y1="0" x2="${cx}" y2="${H}" style="stroke:var(--accent-2);stroke-opacity:0.6" stroke-width="1" vector-effect="non-scaling-stroke"/>`;
  if (state.evals.has(fens[state.ply])) {
    html += `<circle cx="${cx}" cy="${y(state.evals.get(fens[state.ply]))}" r="3.5" style="fill:var(--accent-2);stroke:none"/>`;
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
$('resetBtn').addEventListener('click', () => { if (editor.active) closeEditor(); newGame(); });
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
  if (e.key === 'Escape') {
    if (!$('settingsModal').hidden) closeSettings();
    else if (editor.active) closeEditor();
    return;
  }
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || !$('settingsModal').hidden) return;
  if (e.key === 'ArrowLeft') goTo(state.ply - 1);
  else if (e.key === 'ArrowRight') goTo(state.ply + 1);
  else if (e.key === 'Home') goTo(0);
  else if (e.key === 'End') goTo(state.moves.length);
  else if (e.key.toLowerCase() === 'f') flip();
});

// ---------- settings ----------

const ANALYSIS_KEYS = new Set(['engine', 'movetime', 'multipv', 'threads', 'hash']);

function setSetting(key, value) {
  if (settings[key] === value) return;
  settings[key] = value;
  saveSettings();
  applySettings();
  syncControls();
  if (ANALYSIS_KEYS.has(key)) {
    if (key === 'threads' || key === 'hash') state.analysis = null; // re-run with the new resources
    renderAnalysis();
    renderArrows();
    scheduleAnalysis(true);
  } else if (key === 'auto' && value) {
    scheduleAnalysis(true);
  }
}

function applySettings() {
  const root = document.documentElement;
  root.dataset.theme = settings.theme;
  root.dataset.board = settings.board;
  const cls = document.body.classList;
  cls.toggle('hide-coords', !settings.coords);
  cls.toggle('hide-arrows', !settings.arrows);
  cls.toggle('hide-evalbar', !settings.evalbar);
  cls.toggle('hide-last', !settings.lastMove);
  cls.toggle('hide-dots', !settings.legalDots);
  cls.toggle('hide-graph', !settings.graph);
}

// Reflects `settings` in every control marked with data-setting
function syncControls() {
  document.querySelectorAll('[data-setting]').forEach((el) => {
    const key = el.dataset.setting;
    if (el.classList.contains('toggle')) {
      el.setAttribute('aria-checked', String(!!settings[key]));
    } else {
      el.querySelectorAll('[data-value]').forEach((opt) => {
        const v = el.dataset.type === 'number' ? Number(opt.dataset.value) : opt.dataset.value;
        opt.classList.toggle('active', v === settings[key]);
      });
    }
  });
}

document.addEventListener('click', (e) => {
  const toggle = e.target.closest('.toggle[data-setting]');
  if (toggle) { setSetting(toggle.dataset.setting, !settings[toggle.dataset.setting]); return; }
  const opt = e.target.closest('[data-setting] [data-value]');
  if (opt) {
    const group = opt.closest('[data-setting]');
    setSetting(group.dataset.setting, group.dataset.type === 'number' ? Number(opt.dataset.value) : opt.dataset.value);
    if (group.dataset.setting === 'pieces') { buildPalette(); renderBoard(); }
  }
});

function buildSettingsUI() {
  $('themeGrid').innerHTML = THEMES.map((t) => `
    <button class="theme-card" data-value="${t.id}">
      <div class="theme-preview" style="background:${t.bg}">
        <div class="tp-panel" style="background:${t.panel}"></div>
        <div class="tp-bar" style="background:${t.text}"></div>
        <div class="tp-line" style="background:${t.text}"></div>
        <div class="tp-btn" style="background:linear-gradient(135deg, ${t.a}, ${t.b})"></div>
      </div>
      <div class="swatch-name">${t.name}</div>
    </button>`).join('');
  $('boardGrid').innerHTML = BOARDS.map((b) => `
    <button class="board-swatch" data-value="${b.id}">
      <div class="mini-board">${[0, 1, 2, 3, 4, 5, 6, 7].map((i) => `<i style="background:${(i + (i >> 2)) % 2 ? b.dark : b.light}"></i>`).join('')}</div>
      <div class="swatch-name">${b.name}</div>
    </button>`).join('');
  $('pieceGrid').innerHTML = PIECE_SETS.map((p) => `
    <button class="piece-swatch" data-value="${p.id}">
      <div class="piece-preview"><img src="pieces/${p.id}/wN.svg" alt=""><img src="pieces/${p.id}/bQ.svg" alt=""></div>
      <div class="swatch-name">${p.name}</div>
    </button>`).join('');
  $('versionLabel').textContent = `Version ${APP_VERSION}`;
}

function openSettings(tab) {
  if (tab) showTab(tab);
  $('settingsModal').hidden = false;
}
function closeSettings() { $('settingsModal').hidden = true; }
function showTab(tab) {
  document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t.dataset.tab === tab));
  document.querySelectorAll('.panel').forEach((p) => { p.hidden = p.dataset.panel !== tab; });
}

$('settingsBtn').addEventListener('click', () => openSettings());
$('moreSettingsBtn').addEventListener('click', () => openSettings('engine'));
$('settingsClose').addEventListener('click', closeSettings);
$('settingsModal').addEventListener('click', (e) => { if (e.target.id === 'settingsModal') closeSettings(); });
document.querySelectorAll('.tab').forEach((t) => t.addEventListener('click', () => showTab(t.dataset.tab)));
$('resetSettingsBtn').addEventListener('click', () => {
  const engine = state.engines.some((e) => e.id === 'stockfish') ? 'stockfish' : settings.engine;
  Object.assign(settings, DEFAULT_SETTINGS, { engine });
  saveSettings();
  applySettings();
  syncControls();
  buildPalette();
  renderBoard();
  state.analysis = null;
  scheduleAnalysis(true);
  toast('Settings reset');
});

function loadSettings() {
  try {
    const saved = JSON.parse(localStorage.getItem('chessai-settings') || '{}');
    for (const key of Object.keys(DEFAULT_SETTINGS)) {
      if (key in saved && (DEFAULT_SETTINGS[key] === null || typeof saved[key] === typeof DEFAULT_SETTINGS[key])) settings[key] = saved[key];
    }
  } catch { /* storage unavailable */ }
  if (!THEMES.some((t) => t.id === settings.theme)) settings.theme = DEFAULT_SETTINGS.theme;
  if (!BOARDS.some((b) => b.id === settings.board)) settings.board = DEFAULT_SETTINGS.board;
  if (!PIECE_SETS.some((p) => p.id === settings.pieces)) settings.pieces = DEFAULT_SETTINGS.pieces;
}

function saveSettings() {
  try { localStorage.setItem('chessai-settings', JSON.stringify(settings)); } catch { /* storage unavailable */ }
}

// ---------- main update ----------

function update() {
  game = gameAt(state.ply);
  if (editor.active) { renderEditor(); return; }
  const turn = game.turn();
  const label = $('turnLabel');
  label.classList.remove('editing');
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

loadSettings();
applySettings();
buildSettingsUI();
syncControls();
update();
loadEngines();

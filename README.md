<p align="center"><img src="app/assets/icon.png" width="96" alt="ChessAi"></p>

<h1 align="center">ChessAi</h1>

<p align="center">A desktop chess analysis board powered by <b>Stockfish 19</b>, plus <b>ChessAi</b>, a chess engine written from scratch in C++.</p>

## Download

Grab **ChessAi.exe** from the [latest release](../../releases/latest) and run it. No install needed (Windows 10/11, 64-bit).

> Windows SmartScreen may warn because the app isn't code-signed: click **More info → Run anyway**.

## Features

- **Best move + top lines** — the engine's best move with an arrow on the board, plus the top 3 or 5 alternatives with their continuations
- **Enter moves your way** — type `e4 e5 Nf3`, paste a full PGN or a FEN, or move pieces by clicking or dragging
- **Eval bar and evaluation graph** across the whole game; click the graph to jump to a move
- **Board editor** — set up any position by placing pieces, choose the side to move and castling rights; illegal setups are caught before analysis
- **Two engines** — Stockfish 19 (strongest available, ~3600+ Elo) or the home-made ChessAi engine
- **Settings** — 5 app themes (Amethyst, Midnight, Emerald, Sunset, Light), 5 board colors, 6 piece sets, display toggles, think time, number of lines, CPU threads and hash memory
- Move list with navigation (← → keys), flip board (F), take back, copy FEN

## Build from source

Requirements: Windows, [MSYS2](https://www.msys2.org/) with `mingw-w64-ucrt-x86_64-gcc` and `make`, Python 3.10+.

```
pip install -r requirements.txt
python build.py            # -> dist/ChessAi.exe
```

`build.py` compiles the ChessAi engine, downloads Stockfish, and packages everything with PyInstaller.

For development, run the UI in a browser instead: `python app/server.py` (or `start.bat`).

## Project layout

| Path | What it is |
|---|---|
| `engine/` | ChessAi UCI engine in C++20: magic bitboards, alpha-beta/PVS search, tapered eval ([details](engine/README.md)) |
| `app/server.py` | Local server that drives the UCI engines (standard library only) |
| `app/desktop.py` | Native window wrapper (pywebview / Edge WebView2) |
| `app/static/` | The UI: HTML, CSS, JavaScript |
| `build.py` | One-command release build |

## Credits

- [Stockfish](https://stockfishchess.org/) — GPL-3.0, bundled unmodified in release builds
- [chess.js](https://github.com/jhlywa/chess.js) — BSD-2-Clause, move legality and notation in the UI
- Piece sets via [lichess](https://github.com/lichess-org/lila): cburnett (Colin M.L. Burnett, GPLv2+), merida (Armando Hernandez Marroquin, GPLv2+), chessnut (Alexis Luengas, Apache-2.0), spatial and fantasy (Maurizio Monge, MIT), mpchess (Maxime Chupin, GPLv3+)
- Piece-square tables from PeSTO by Ronald Friederich

## License

GPL-3.0 — see [LICENSE](LICENSE).

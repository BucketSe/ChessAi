# ChessAi engine

A UCI chess engine in C++20. Any UCI GUI (Arena, Cute Chess, BanksiaGUI) or a future web UI can drive it.

## Build

```
mingw32-make        # Windows (MSYS2 g++) -> chessai.exe
make                # Linux / macOS
```

## Test

```
chessai.exe perftsuite 5      # move-generator correctness (standard perft positions)
chessai.exe bench 12          # fixed-depth search benchmark, prints nodes/sec
python tests/selfplay.py      # engine-vs-engine games, every move validated by python-chess
python tests/selfplay.py --opponent C:\path\to\stockfish.exe --games 10
```

## Talking to it (UCI)

```
uci
position startpos moves e2e4 e7e5
go wtime 60000 btime 60000 winc 1000 binc 1000   # or: go depth 15 / go movetime 2000 / go infinite
stop
```

Extra debug commands: `d` (print board), `eval`, `perft <n>`, `bench <depth>`.

## Layout

| File | Purpose |
|---|---|
| `src/bitboard.*` | Bitboards, magic sliding-piece attacks, leaper tables |
| `src/position.*` | Board state, FEN, make/unmake, Zobrist hashing, repetition |
| `src/movegen.*` | Pseudo-legal move generation, perft |
| `src/eval.*` | Tapered PeSTO evaluation + pawn structure, mobility, king shield, mop-up |
| `src/search.*` | Iterative deepening, aspiration windows, PVS alpha-beta, quiescence, time management |
| `src/tt.*` | Transposition table |
| `src/uci.*` | UCI protocol loop, bench and perft suite |

Search techniques used: transposition table, null-move pruning, reverse futility pruning, futility pruning, late move pruning and reductions, internal iterative reduction, check extension, mate-distance pruning, SEE-based capture ordering and qsearch pruning, killer moves, history heuristic.

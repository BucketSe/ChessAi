"""Play UCI engines against each other, validating every move with python-chess.

Usage:
    python tests/selfplay.py                          # ChessAi vs itself
    python tests/selfplay.py --opponent path/to/stockfish.exe --games 10
"""
import argparse
import pathlib
import random

import chess
import chess.engine
import chess.pgn

ENGINE = str(pathlib.Path(__file__).resolve().parent.parent / "chessai.exe")
OPENINGS = ["", "e2e4 e7e5", "d2d4 d7d5", "e2e4 c7c5", "c2c4 e7e5", "d2d4 g8f6 c2c4 e7e6", "g1f3 d7d5"]


def play_game(white, black, base, inc, opening):
    board = chess.Board()
    for uci in opening.split():
        board.push_uci(uci)
    clock = {chess.WHITE: base, chess.BLACK: base}
    depths = []
    while not board.is_game_over(claim_draw=True):
        engine = white if board.turn == chess.WHITE else black
        limit = chess.engine.Limit(white_clock=clock[chess.WHITE], black_clock=clock[chess.BLACK],
                                   white_inc=inc, black_inc=inc)
        result = engine.play(board, limit, info=chess.engine.INFO_BASIC)
        clock[board.turn] -= result.info.get("time", 0)
        if clock[board.turn] < 0:
            return ("0-1" if board.turn == chess.WHITE else "1-0"), board, depths, "time forfeit"
        clock[board.turn] += inc
        if result.move not in board.legal_moves:
            raise RuntimeError(f"Illegal move {result.move} in {board.fen()}")
        depths.append(result.info.get("depth", 0))
        board.push(result.move)
    outcome = board.outcome(claim_draw=True)
    return board.result(claim_draw=True), board, depths, outcome.termination.name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--opponent", default=ENGINE)
    ap.add_argument("--games", type=int, default=4)
    ap.add_argument("--time", type=float, default=5.0, help="base seconds per side")
    ap.add_argument("--inc", type=float, default=0.05)
    ap.add_argument("--pgn", default=None)
    args = ap.parse_args()

    ours = chess.engine.SimpleEngine.popen_uci(ENGINE)
    theirs = chess.engine.SimpleEngine.popen_uci(args.opponent)
    score = {"ours": 0.0, "theirs": 0.0}
    try:
        for g in range(args.games):
            opening = random.choice(OPENINGS)
            ours_white = g % 2 == 0
            white, black = (ours, theirs) if ours_white else (theirs, ours)
            result, board, depths, how = play_game(white, black, args.time, args.inc, opening)
            if result == "1/2-1/2":
                score["ours"] += 0.5
                score["theirs"] += 0.5
            elif (result == "1-0") == ours_white:
                score["ours"] += 1
            else:
                score["theirs"] += 1
            avg = sum(depths) / max(1, len(depths))
            print(f"game {g + 1}: {result} ({how}), {len(board.move_stack)} plies, avg depth {avg:.1f}, "
                  f"ChessAi as {'white' if ours_white else 'black'}")
            if args.pgn:
                with open(args.pgn, "a") as f:
                    print(chess.pgn.Game.from_board(board), file=f, end="\n\n")
    finally:
        ours.quit()
        theirs.quit()
    print(f"ChessAi {score['ours']} - {score['theirs']} opponent")


if __name__ == "__main__":
    main()

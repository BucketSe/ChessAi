#pragma once
#include "position.h"

struct MoveList {
    Move moves[MAX_MOVES];
    int scores[MAX_MOVES];
    int count = 0;
    void add(Move m) { moves[count++] = m; }
};

// Pseudo-legal moves; legality is checked by Position::make_move.
// captures_only also includes queen promotions (used by quiescence search).
void generate_moves(const Position& pos, MoveList& list, bool captures_only = false);

U64 perft(Position& pos, int depth);

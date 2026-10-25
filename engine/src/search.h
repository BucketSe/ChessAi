#pragma once
#include "position.h"
#include <atomic>

struct SearchLimits {
    int depth = MAX_PLY - 1;
    long long wtime = -1, btime = -1, winc = 0, binc = 0;
    long long movetime = -1;
    int movestogo = 0;
    U64 nodes = 0;
    bool infinite = false;
    int multipv = 1; // number of best root lines to report
};

struct SearchResult {
    Move best = 0;
    int score = 0;
    int depth = 0;
    U64 nodes = 0;
};

extern std::atomic<bool> g_stop;

void init_search();
void clear_search(); // forget history/killers (ucinewgame)

// Runs iterative deepening. With print=true it emits UCI "info" lines and the final "bestmove".
SearchResult search_position(Position& pos, const SearchLimits& lim, bool print = true);

#include "bitboard.h"
#include "eval.h"
#include "position.h"
#include "search.h"
#include "tt.h"
#include "uci.h"
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    init_bitboards();
    init_position();
    init_eval();
    init_search();
    TT.resize(64);

    if (argc > 1) {
        std::string cmd = argv[1];
        int depth = argc > 2 ? std::atoi(argv[2]) : 0;
        if (cmd == "bench") {
            run_bench(depth > 0 ? depth : 10);
            return 0;
        }
        if (cmd == "perftsuite") return run_perft_suite(depth > 0 ? depth : 5) ? 0 : 1;
    }

    uci_loop();
    return 0;
}

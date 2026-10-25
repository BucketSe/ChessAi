#include "uci.h"
#include "eval.h"
#include "movegen.h"
#include "search.h"
#include "tt.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace {

std::mutex io_mutex;
int multipv_option = 1;

long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void set_position(Position& pos, std::istringstream& ss) {
    std::string token, fen;
    ss >> token;
    if (token == "startpos") {
        fen = START_FEN;
        ss >> token; // "moves" (if present)
    } else if (token == "fen") {
        while (ss >> token && token != "moves") fen += token + " ";
    } else {
        return;
    }
    pos.set_fen(fen);
    while (ss >> token) {
        Move m = pos.parse_move(token);
        if (!m) {
            send("info string illegal move " + token);
            break;
        }
        pos.make_move(m);
    }
}

SearchLimits parse_go(std::istringstream& ss) {
    SearchLimits lim;
    bool limited = false;
    std::string token;
    while (ss >> token) {
        if (token == "wtime") ss >> lim.wtime, limited = true;
        else if (token == "btime") ss >> lim.btime, limited = true;
        else if (token == "winc") ss >> lim.winc;
        else if (token == "binc") ss >> lim.binc;
        else if (token == "movestogo") ss >> lim.movestogo;
        else if (token == "depth") ss >> lim.depth, limited = true;
        else if (token == "nodes") ss >> lim.nodes, limited = true;
        else if (token == "movetime") ss >> lim.movetime, limited = true;
        else if (token == "infinite" || token == "ponder") lim.infinite = true;
    }
    if (!limited) lim.infinite = true;
    lim.multipv = multipv_option;
    return lim;
}

void perft_divide(Position& pos, int depth) {
    long long t0 = now_ms();
    MoveList list;
    generate_moves(pos, list);
    U64 total = 0;
    for (int i = 0; i < list.count; i++) {
        if (!pos.make_move(list.moves[i])) continue;
        U64 n = depth > 1 ? perft(pos, depth - 1) : 1;
        pos.unmake_move(list.moves[i]);
        total += n;
        send(move_to_uci(list.moves[i]) + ": " + std::to_string(n));
    }
    long long t = std::max(1LL, now_ms() - t0);
    send("\nNodes searched: " + std::to_string(total) + "  (" + std::to_string(t) + " ms, " +
         std::to_string(total * 1000 / t) + " nps)");
}

} // namespace

void send(const std::string& line) {
    std::lock_guard<std::mutex> lock(io_mutex);
    std::cout << line << std::endl;
}

void uci_loop() {
    Position pos, search_pos;
    std::thread worker;
    auto stop_search = [&] {
        g_stop = true;
        if (worker.joinable()) worker.join();
    };

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "uci") {
            send("id name ChessAi 1.0");
            send("id author ChessAi");
            send("option name Hash type spin default 64 min 1 max 4096");
            send("option name Clear Hash type button");
            send("option name MultiPV type spin default 1 min 1 max 10");
            send("uciok");
        } else if (cmd == "isready") {
            send("readyok");
        } else if (cmd == "ucinewgame") {
            stop_search();
            TT.clear();
            clear_search();
        } else if (cmd == "position") {
            stop_search();
            set_position(pos, ss);
        } else if (cmd == "go") {
            stop_search();
            SearchLimits lim = parse_go(ss);
            search_pos = pos;
            g_stop = false;
            worker = std::thread([&search_pos, lim] { search_position(search_pos, lim); });
        } else if (cmd == "stop") {
            stop_search();
        } else if (cmd == "ponderhit") {
            // pondering is treated as an infinite search; the GUI follows up with "stop"
        } else if (cmd == "setoption") {
            std::string token, name, value;
            bool in_value = false;
            ss >> token; // "name"
            while (ss >> token) {
                if (token == "value") in_value = true;
                else if (in_value) value += (value.empty() ? "" : " ") + token;
                else name += (name.empty() ? "" : " ") + token;
            }
            stop_search();
            if (name == "Hash" && !value.empty()) TT.resize(std::max(1, std::stoi(value)));
            else if (name == "Clear Hash") TT.clear();
            else if (name == "MultiPV" && !value.empty()) multipv_option = std::clamp(std::stoi(value), 1, 10);
        } else if (cmd == "quit") {
            break;
        }
        // Non-UCI debugging commands
        else if (cmd == "d") {
            pos.print();
        } else if (cmd == "eval") {
            send("Static eval (side to move): " + std::to_string(evaluate(pos)) + " cp");
        } else if (cmd == "perft") {
            int depth = 1;
            ss >> depth;
            stop_search();
            perft_divide(pos, depth);
        } else if (cmd == "bench") {
            int depth = 10;
            ss >> depth;
            stop_search();
            run_bench(depth);
        } else if (cmd == "perftsuite") {
            int depth = 5;
            ss >> depth;
            stop_search();
            run_perft_suite(depth);
        } else if (!cmd.empty()) {
            send("Unknown command: " + cmd);
        }
    }
    stop_search();
}

void run_bench(int depth) {
    static const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
        "2rq1rk1/pp1bppbp/3p1np1/8/3NP3/1BN1BP2/PPPQ2PP/2KR3R b - - 0 12",
        "8/8/4k3/8/8/8/3QK3/8 w - - 0 1",
    };
    Position pos;
    SearchLimits lim;
    lim.depth = depth;
    U64 total = 0;
    long long t0 = now_ms();
    for (const char* fen : fens) {
        pos.set_fen(fen);
        TT.clear();
        clear_search();
        g_stop = false;
        SearchResult r = search_position(pos, lim, false);
        total += r.nodes;
        std::string score = std::abs(r.score) >= MATE_BOUND ? (r.score > 0 ? "mate+" : "mate-")
                                                            : std::to_string(r.score) + "cp";
        send(std::string(fen) + "\n    bestmove " + move_to_uci(r.best) + "  score " + score + "  nodes " +
             std::to_string(r.nodes));
    }
    long long t = std::max(1LL, now_ms() - t0);
    send("\nBench: " + std::to_string(total) + " nodes  " + std::to_string(t) + " ms  " +
         std::to_string(total * 1000 / t) + " nps");
}

bool run_perft_suite(int max_depth) {
    struct Case {
        const char* fen;
        U64 expected[7]; // index = depth
    };
    static const Case cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", {1, 20, 400, 8902, 197281, 4865609, 119060324}},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", {1, 48, 2039, 97862, 4085603, 193690690, 0}},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", {1, 14, 191, 2812, 43238, 674624, 11030083}},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", {1, 6, 264, 9467, 422333, 15833292, 0}},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", {1, 44, 1486, 62379, 2103487, 89941194, 0}},
        {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", {1, 46, 2079, 89890, 3894594, 164075551, 0}},
    };
    Position pos;
    bool all_ok = true;
    long long t0 = now_ms();
    for (const Case& c : cases) {
        pos.set_fen(c.fen);
        for (int d = 1; d <= max_depth && d < 7 && c.expected[d]; d++) {
            U64 n = perft(pos, d);
            bool ok = n == c.expected[d];
            all_ok &= ok;
            send(std::string(ok ? "PASS" : "FAIL") + "  depth " + std::to_string(d) + "  " + std::to_string(n) +
                 (ok ? "" : " (expected " + std::to_string(c.expected[d]) + ")") + "  " + c.fen);
        }
    }
    send(std::string(all_ok ? "All perft tests passed" : "PERFT FAILURES") + " in " +
         std::to_string(now_ms() - t0) + " ms");
    return all_ok;
}

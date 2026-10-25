#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "tt.h"
#include "uci.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

std::atomic<bool> g_stop{false};

namespace {

using Clock = std::chrono::steady_clock;

constexpr int SEE_VAL[6] = {100, 320, 330, 500, 900, 20000};

Clock::time_point start_time;
long long soft_limit, hard_limit; // ms, -1 = unlimited
U64 node_limit;
U64 nodes;
bool stopped;
int seldepth;

Move killers[MAX_PLY + 1][2];
int history_table[2][64][64];
Move pv[MAX_PLY + 1][MAX_PLY + 1];
int pv_len[MAX_PLY + 1];
int lmr_table[64][64];
std::vector<Move> excluded_root; // root moves already reported as better MultiPV lines

struct RootLine {
    int score;
    std::vector<Move> pv;
};

long long elapsed_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_time).count();
}

void check_time() {
    if (g_stop.load(std::memory_order_relaxed)) stopped = true;
    else if (hard_limit >= 0 && elapsed_ms() >= hard_limit) stopped = true;
    else if (node_limit && nodes >= node_limit) stopped = true;
}

int score_to_tt(int s, int ply) { return s >= MATE_BOUND ? s + ply : s <= -MATE_BOUND ? s - ply : s; }
int score_from_tt(int s, int ply) { return s >= MATE_BOUND ? s - ply : s <= -MATE_BOUND ? s + ply : s; }

int captured_type(const Position& pos, Move m) {
    return flag_of(m) == EP_CAPTURE ? PAWN : type_of(pos.board[to_sq(m)]);
}

// Static exchange evaluation: material outcome of the capture sequence on the target square.
int see(const Position& pos, Move m) {
    const int from = from_sq(m), to = to_sq(m);
    int gain[32], d = 0;
    Bitboard occupied = pos.occ[2];
    gain[0] = is_capture(m) ? SEE_VAL[captured_type(pos, m)] : 0;
    int attacker = type_of(pos.board[from]);
    int stm = pos.side;

    Bitboard from_bb = bit(from);
    if (flag_of(m) == EP_CAPTURE) occupied ^= bit(to + (stm == WHITE ? -8 : 8));

    do {
        d++;
        gain[d] = SEE_VAL[attacker] - gain[d - 1];
        if (std::max(-gain[d - 1], gain[d]) < 0) break;
        occupied ^= from_bb;
        Bitboard attackers = pos.attackers_to(to, occupied) & occupied; // reveals x-ray attackers
        stm ^= 1;
        Bitboard mine = attackers & pos.occ[stm];
        from_bb = 0;
        for (int pt = PAWN; pt <= KING; pt++) {
            Bitboard b = mine & pos.pieces[make_piece(stm, pt)];
            if (b) {
                from_bb = b & -b;
                attacker = pt;
                break;
            }
        }
    } while (from_bb && d < 31);

    while (--d) gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    return gain[0];
}

void score_moves(const Position& pos, MoveList& list, Move tt_move, int ply) {
    for (int i = 0; i < list.count; i++) {
        const Move m = list.moves[i];
        int s;
        if (m == tt_move) {
            s = 30000000;
        } else if (is_capture(m)) {
            int victim = captured_type(pos, m), attacker = type_of(pos.board[from_sq(m)]);
            int mvv_lva = SEE_VAL[victim] * 10 - attacker;
            bool good = SEE_VAL[victim] >= SEE_VAL[attacker] || see(pos, m) >= 0;
            s = (good ? 20000000 : -10000000) + mvv_lva;
            if (is_promo(m) && promo_type(m) == QUEEN) s += 1000000;
        } else if (is_promo(m)) {
            s = promo_type(m) == QUEEN ? 19000000 : -20000000;
        } else if (m == killers[ply][0]) {
            s = 18000000;
        } else if (m == killers[ply][1]) {
            s = 17000000;
        } else {
            s = history_table[pos.side][from_sq(m)][to_sq(m)];
        }
        list.scores[i] = s;
    }
}

// Selection sort step: bring the best remaining move to index i.
void pick_move(MoveList& list, int i) {
    int best = i;
    for (int j = i + 1; j < list.count; j++)
        if (list.scores[j] > list.scores[best]) best = j;
    std::swap(list.moves[i], list.moves[best]);
    std::swap(list.scores[i], list.scores[best]);
}

void update_pv(int ply, Move m) {
    pv[ply][ply] = m;
    for (int j = ply + 1; j < pv_len[ply + 1]; j++) pv[ply][j] = pv[ply + 1][j];
    pv_len[ply] = std::max(pv_len[ply + 1], ply + 1);
}

void update_history(int side, Move m, int bonus) {
    int& h = history_table[side][from_sq(m)][to_sq(m)];
    h += bonus - h * std::abs(bonus) / 16384;
}

int qsearch(Position& pos, int alpha, int beta, int ply) {
    pv_len[ply] = ply;
    if ((++nodes & 2047) == 0) check_time();
    if (stopped) return 0;
    if (ply > seldepth) seldepth = ply;
    if (ply >= MAX_PLY) return evaluate(pos);

    const bool in_check = pos.in_check();
    int best = -INF, stand_pat = 0;
    if (!in_check) {
        stand_pat = evaluate(pos);
        if (stand_pat >= beta) return stand_pat;
        if (stand_pat > alpha) alpha = stand_pat;
        best = stand_pat;
    }

    MoveList list;
    generate_moves(pos, list, !in_check); // all evasions when in check
    score_moves(pos, list, 0, ply);

    int legal = 0;
    for (int i = 0; i < list.count; i++) {
        pick_move(list, i);
        const Move m = list.moves[i];
        if (!in_check && is_capture(m) && !is_promo(m)) {
            if (stand_pat + SEE_VAL[captured_type(pos, m)] + 200 <= alpha) continue; // delta pruning
            if (list.scores[i] < 0) continue;                                       // losing capture
        }
        if (!pos.make_move(m)) continue;
        legal++;
        int score = -qsearch(pos, -beta, -alpha, ply + 1);
        pos.unmake_move(m);
        if (stopped) return 0;

        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                update_pv(ply, m);
                if (score >= beta) break;
            }
        }
    }
    if (in_check && legal == 0) return -MATE + ply;
    return best;
}

int negamax(Position& pos, int alpha, int beta, int depth, int ply, bool do_null) {
    const bool pv_node = beta - alpha > 1;
    const bool root = ply == 0;
    pv_len[ply] = ply;

    if (!root) {
        if (pos.halfmove >= 100 || pos.is_repetition() || pos.insufficient_material()) return 0;
        // Mate distance pruning
        alpha = std::max(alpha, -MATE + ply);
        beta = std::min(beta, MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    const bool in_check = pos.in_check();
    if (in_check) depth++; // check extension
    if (depth <= 0) return qsearch(pos, alpha, beta, ply);

    if ((++nodes & 2047) == 0) check_time();
    if (stopped) return 0;
    if (ply >= MAX_PLY) return evaluate(pos);
    if (ply > seldepth) seldepth = ply;

    Move tt_move = 0;
    if (const TTEntry* e = TT.probe(pos.hash)) {
        tt_move = e->move;
        if (!pv_node && e->depth >= depth) {
            int s = score_from_tt(e->score, ply);
            if (e->flag == TT_EXACT || (e->flag == TT_LOWER && s >= beta) || (e->flag == TT_UPPER && s <= alpha))
                return s;
        }
    }

    const int static_eval = in_check ? -INF : evaluate(pos);

    if (!pv_node && !in_check) {
        // Reverse futility pruning
        if (depth <= 7 && static_eval - 80 * depth >= beta && std::abs(beta) < MATE_BOUND) return static_eval;

        // Null move pruning
        if (do_null && depth >= 3 && static_eval >= beta && pos.has_non_pawn_material(pos.side)) {
            int R = 3 + depth / 4 + std::min((static_eval - beta) / 200, 3);
            pos.make_null();
            int s = -negamax(pos, -beta, -beta + 1, depth - 1 - R, ply + 1, false);
            pos.unmake_null();
            if (stopped) return 0;
            if (s >= beta) return s >= MATE_BOUND ? beta : s;
        }
    }

    // Internal iterative reduction: without a hash move this node is probably not critical
    if (depth >= 4 && !tt_move) depth--;

    MoveList list;
    generate_moves(pos, list);
    score_moves(pos, list, tt_move, ply);

    const int orig_alpha = alpha;
    int best = -INF, legal = 0;
    Move best_move = 0;
    Move quiets[64];
    int n_quiets = 0;

    for (int i = 0; i < list.count; i++) {
        pick_move(list, i);
        const Move m = list.moves[i];
        const bool quiet = !is_capture(m) && !is_promo(m);
        if (root && std::find(excluded_root.begin(), excluded_root.end(), m) != excluded_root.end()) continue;

        if (!root && !in_check && quiet && legal > 0 && best > -MATE_BOUND) {
            // Late move pruning
            if (!pv_node && depth <= 3 && legal >= 3 + depth * depth) continue;
            // Futility pruning
            if (!pv_node && depth <= 4 && static_eval + 100 + 100 * depth <= alpha) continue;
        }

        if (!pos.make_move(m)) continue;
        legal++;
        const bool gives_check = pos.in_check();

        int score;
        if (legal == 1) {
            score = -negamax(pos, -beta, -alpha, depth - 1, ply + 1, true);
        } else {
            // Late move reductions
            int R = 0;
            if (depth >= 3 && quiet && !in_check && !gives_check && legal > 1 + pv_node) {
                R = lmr_table[std::min(depth, 63)][std::min(legal, 63)];
                if (pv_node) R--;
                if (m == killers[ply][0] || m == killers[ply][1]) R--;
                R -= history_table[pos.side ^ 1][from_sq(m)][to_sq(m)] / 8192;
                R = std::clamp(R, 0, depth - 2);
            }
            score = -negamax(pos, -alpha - 1, -alpha, depth - 1 - R, ply + 1, true);
            if (score > alpha && R > 0) score = -negamax(pos, -alpha - 1, -alpha, depth - 1, ply + 1, true);
            if (score > alpha && score < beta) score = -negamax(pos, -beta, -alpha, depth - 1, ply + 1, true);
        }
        pos.unmake_move(m);
        if (stopped) return 0;

        if (score > best) {
            best = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
                update_pv(ply, m);
                if (score >= beta) {
                    if (quiet) {
                        if (killers[ply][0] != m) {
                            killers[ply][1] = killers[ply][0];
                            killers[ply][0] = m;
                        }
                        int bonus = std::min(depth * depth, 1600);
                        update_history(pos.side, m, bonus);
                        for (int q = 0; q < n_quiets; q++) update_history(pos.side, quiets[q], -bonus);
                    }
                    break;
                }
            }
        }
        if (quiet && n_quiets < 64) quiets[n_quiets++] = m;
    }

    if (legal == 0) return in_check ? -MATE + ply : 0;

    // With excluded root moves the root score is not the position's true score
    if (!root || excluded_root.empty()) {
        int flag = best >= beta ? TT_LOWER : alpha > orig_alpha ? TT_EXACT : TT_UPPER;
        TT.store(pos.hash, best_move, score_to_tt(best, ply), depth, flag);
    }
    return best;
}

std::string score_string(int score) {
    if (score >= MATE_BOUND) return "mate " + std::to_string((MATE - score + 1) / 2);
    if (score <= -MATE_BOUND) return "mate -" + std::to_string((MATE + score) / 2);
    return "cp " + std::to_string(score);
}

void set_time_limits(const Position& pos, const SearchLimits& lim) {
    soft_limit = hard_limit = -1;
    const long long mytime = pos.side == WHITE ? lim.wtime : lim.btime;
    const long long inc = pos.side == WHITE ? lim.winc : lim.binc;
    if (lim.infinite) return;
    if (lim.movetime >= 0) {
        soft_limit = hard_limit = std::max(1LL, lim.movetime - 10);
    } else if (mytime >= 0) {
        const long long avail = std::max(1LL, mytime - 30); // keep a safety margin for GUI lag
        const int mtg = lim.movestogo > 0 ? std::min(lim.movestogo, 40) : 30;
        long long target = std::min(avail / mtg + inc * 3 / 4, avail * 8 / 10);
        soft_limit = std::max(1LL, target * 6 / 10);
        hard_limit = std::max(1LL, std::min(target * 3, avail * 8 / 10));
    }
}

} // namespace

void init_search() {
    for (int d = 1; d < 64; d++)
        for (int m = 1; m < 64; m++) lmr_table[d][m] = int(0.75 + std::log(d) * std::log(m) / 2.25);
}

void clear_search() {
    std::memset(killers, 0, sizeof(killers));
    std::memset(history_table, 0, sizeof(history_table));
}

SearchResult search_position(Position& pos, const SearchLimits& lim, bool print) {
    start_time = Clock::now();
    nodes = 0;
    stopped = false;
    node_limit = lim.nodes;
    set_time_limits(pos, lim);

    std::memset(killers, 0, sizeof(killers));
    for (auto& side : history_table)
        for (auto& from : side)
            for (int& h : from) h /= 2;
    TT.new_search();

    int legal_root = 0;
    {
        MoveList list;
        generate_moves(pos, list);
        for (int i = 0; i < list.count; i++)
            if (pos.make_move(list.moves[i])) pos.unmake_move(list.moves[i]), legal_root++;
    }
    const int multipv = std::clamp(lim.multipv, 1, std::max(1, legal_root));

    SearchResult result;
    std::vector<RootLine> prev_lines;
    for (int depth = 1; depth <= std::min(lim.depth, MAX_PLY - 1); depth++) {
        std::vector<RootLine> lines;
        excluded_root.clear();
        for (int k = 0; k < multipv; k++) {
            seldepth = 0;
            const int prev = k < int(prev_lines.size()) ? prev_lines[k].score : 0;
            int delta = 20, alpha = -INF, beta = INF, score = 0;
            if (depth >= 5) {
                alpha = std::max(prev - delta, -INF);
                beta = std::min(prev + delta, INF);
            }
            for (;;) { // aspiration windows
                score = negamax(pos, alpha, beta, depth, 0, false);
                if (stopped) break;
                if (score <= alpha) {
                    beta = (alpha + beta) / 2;
                    alpha = std::max(score - delta, -INF);
                } else if (score >= beta) {
                    beta = std::min(score + delta, INF);
                } else {
                    break;
                }
                delta += delta / 2;
                if (delta > 1000) alpha = -INF, beta = INF;
            }
            if (stopped || pv_len[0] == 0) break;
            lines.push_back({score, std::vector<Move>(pv[0], pv[0] + pv_len[0])});
            excluded_root.push_back(pv[0][0]);
        }
        excluded_root.clear();
        if (stopped || int(lines.size()) < multipv) break;

        std::stable_sort(lines.begin(), lines.end(), [](const RootLine& a, const RootLine& b) { return a.score > b.score; });
        prev_lines = lines;
        result.best = lines[0].pv[0];
        result.score = lines[0].score;
        result.depth = depth;

        if (print) {
            long long t = elapsed_ms();
            for (int k = 0; k < multipv; k++) {
                std::string line = "info depth " + std::to_string(depth) + " seldepth " + std::to_string(seldepth) +
                                   " multipv " + std::to_string(k + 1) + " score " + score_string(lines[k].score) +
                                   " nodes " + std::to_string(nodes) + " nps " +
                                   std::to_string(nodes * 1000 / std::max(1LL, t)) + " hashfull " +
                                   std::to_string(TT.hashfull()) + " time " + std::to_string(t) + " pv";
                for (Move m : lines[k].pv) line += " " + move_to_uci(m);
                send(line);
            }
        }
        if (soft_limit >= 0 && elapsed_ms() >= soft_limit) break;
    }

    if (!result.best) { // stopped before depth 1 finished: play any legal move
        MoveList list;
        generate_moves(pos, list);
        for (int i = 0; i < list.count; i++) {
            if (pos.make_move(list.moves[i])) {
                pos.unmake_move(list.moves[i]);
                result.best = list.moves[i];
                break;
            }
        }
    }
    result.nodes = nodes;

    // UCI: in infinite mode bestmove must wait for "stop"
    while (lim.infinite && !g_stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    if (print) send("bestmove " + move_to_uci(result.best));
    return result;
}

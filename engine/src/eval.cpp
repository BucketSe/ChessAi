#include "eval.h"
#include <algorithm>
#include <cstdlib>

// Tapered evaluation: every term has a middlegame (mg) and endgame (eg) value,
// blended by game phase. Piece-square tables are PeSTO's (Ronald Friederich),
// written from White's view with a8 first.
namespace {

const int mg_value[6] = {82, 337, 365, 477, 1025, 0};
const int eg_value[6] = {94, 281, 297, 512, 936, 0};
const int phase_inc[6] = {0, 1, 1, 2, 4, 0};

// clang-format off
const int mg_pawn[64] = {
      0,   0,   0,   0,   0,   0,  0,   0,
     98, 134,  61,  95,  68, 126, 34, -11,
     -6,   7,  26,  31,  65,  56, 25, -20,
    -14,  13,   6,  21,  23,  12, 17, -23,
    -27,  -2,  -5,  12,  17,   6, 10, -25,
    -26,  -4,  -4, -10,   3,   3, 33, -12,
    -35,  -1, -20, -23, -15,  24, 38, -22,
      0,   0,   0,   0,   0,   0,  0,   0,
};
const int eg_pawn[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
    178, 173, 158, 134, 147, 132, 165, 187,
     94, 100,  85,  67,  56,  53,  82,  84,
     32,  24,  13,   5,  -2,   4,  17,  17,
     13,   9,  -3,  -7,  -7,  -8,   3,  -1,
      4,   7,  -6,   1,   0,  -5,  -1,  -8,
     13,   8,   8,  10,  13,   0,   2,  -7,
      0,   0,   0,   0,   0,   0,   0,   0,
};
const int mg_knight[64] = {
    -167, -89, -34, -49,  61, -97, -15, -107,
     -73, -41,  72,  36,  23,  62,   7,  -17,
     -47,  60,  37,  65,  84, 129,  73,   44,
      -9,  17,  19,  53,  37,  69,  18,   22,
     -13,   4,  16,  13,  28,  19,  21,   -8,
     -23,  -9,  12,  10,  19,  17,  25,  -16,
     -29, -53, -12,  -3,  -1,  18, -14,  -19,
    -105, -21, -58, -33, -17, -28, -19,  -23,
};
const int eg_knight[64] = {
    -58, -38, -13, -28, -31, -27, -63, -99,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -29, -51, -23, -15, -22, -18, -50, -64,
};
const int mg_bishop[64] = {
    -29,   4, -82, -37, -25, -42,   7,  -8,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -16,  37,  43,  40,  35,  50,  37,  -2,
     -4,   5,  19,  50,  37,  37,   7,  -2,
     -6,  13,  13,  26,  34,  12,  10,   4,
      0,  15,  15,  15,  14,  27,  18,  10,
      4,  15,  16,   0,   7,  21,  33,   1,
    -33,  -3, -14, -21, -13, -12, -39, -21,
};
const int eg_bishop[64] = {
    -14, -21, -11,  -8, -7,  -9, -17, -24,
     -8,  -4,   7, -12, -3, -13,  -4, -14,
      2,  -8,   0,  -1, -2,   6,   0,   4,
     -3,   9,  12,   9, 14,  10,   3,   2,
     -6,   3,  13,  19,  7,  10,  -3,  -9,
    -12,  -3,   8,  10, 13,   3,  -7, -15,
    -14, -18,  -7,  -1,  4,  -9, -15, -27,
    -23,  -9, -23,  -5, -9, -16,  -5, -17,
};
const int mg_rook[64] = {
     32,  42,  32,  51, 63,  9,  31,  43,
     27,  32,  58,  62, 80, 67,  26,  44,
     -5,  19,  26,  36, 17, 45,  61,  16,
    -24, -11,   7,  26, 24, 35,  -8, -20,
    -36, -26, -12,  -1,  9, -7,   6, -23,
    -45, -25, -16, -17,  3,  0,  -5, -33,
    -44, -16, -20,  -9, -1, 11,  -6, -71,
    -19, -13,   1,  17, 16,  7, -37, -26,
};
const int eg_rook[64] = {
    13, 10, 18, 15, 12,  12,   8,   5,
    11, 13, 13, 11, -3,   3,   8,   3,
     7,  7,  7,  5,  4,  -3,  -5,  -3,
     4,  3, 13,  1,  2,   1,  -1,   2,
     3,  5,  8,  4, -5,  -6,  -8, -11,
    -4,  0, -5, -1, -7, -12,  -8, -16,
    -6, -6,  0,  2, -9,  -9, -11,  -3,
    -9,  2,  3, -1, -5, -13,   4, -20,
};
const int mg_queen[64] = {
    -28,   0,  29,  12,  59,  44,  43,  45,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -13, -17,   7,   8,  29,  56,  47,  57,
    -27, -27, -16, -16,  -1,  17,  -2,   1,
     -9, -26,  -9, -10,  -2,  -4,   3,  -3,
    -14,   2, -11,  -2,  -5,   2,  14,   5,
    -35,  -8,  11,   2,   8,  15,  -3,   1,
     -1, -18,  -9,  10, -15, -25, -31, -50,
};
const int eg_queen[64] = {
     -9,  22,  22,  27,  27,  19,  10,  20,
    -17,  20,  32,  41,  58,  25,  30,   0,
    -20,   6,   9,  49,  47,  35,  19,   9,
      3,  22,  24,  45,  57,  40,  57,  36,
    -18,  28,  19,  47,  31,  34,  39,  23,
    -16, -27,  15,   6,   9,  17,  10,   5,
    -22, -23, -30, -16, -16, -23, -36, -32,
    -33, -28, -22, -43,  -5, -32, -20, -41,
};
const int mg_king[64] = {
    -65,  23,  16, -15, -56, -34,   2,  13,
     29,  -1, -20,  -7,  -8,  -4, -38, -29,
     -9,  24,   2, -16, -20,   6,  22, -22,
    -17, -20, -12, -27, -30, -25, -14, -36,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -14, -14, -22, -46, -44, -30, -15, -27,
      1,   7,  -8, -64, -43, -16,   9,   8,
    -15,  36,  12, -54,   8, -28,  24,  14,
};
const int eg_king[64] = {
    -74, -35, -18, -18, -11,  15,   4, -17,
    -12,  17,  14,  17,  17,  38,  23,  11,
     10,  17,  23,  15,  20,  45,  44,  13,
     -8,  22,  24,  27,  26,  33,  26,   3,
    -18,  -4,  21,  24,  27,  23,   9, -11,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -53, -34, -21, -11, -28, -14, -24, -43,
};
// clang-format on

const int* const mg_pesto[6] = {mg_pawn, mg_knight, mg_bishop, mg_rook, mg_queen, mg_king};
const int* const eg_pesto[6] = {eg_pawn, eg_knight, eg_bishop, eg_rook, eg_queen, eg_king};

// Indexed by relative rank (0 = own back rank)
const int passed_mg[8] = {0, 0, 5, 10, 20, 35, 55, 0};
const int passed_eg[8] = {0, 5, 10, 20, 35, 60, 100, 0};

int mg_table[12][64], eg_table[12][64];

int center_distance(int sq) {
    int f = file_of(sq), r = rank_of(sq);
    return std::max(3 - f, f - 4) + std::max(3 - r, r - 4);
}

Bitboard pawn_attacks_bb(Bitboard pawns, int c) {
    return c == WHITE ? ((pawns << 9) & ~FILE_A_BB) | ((pawns << 7) & ~FILE_H_BB)
                      : ((pawns >> 7) & ~FILE_A_BB) | ((pawns >> 9) & ~FILE_H_BB);
}

} // namespace

void init_eval() {
    for (int pt = PAWN; pt <= KING; pt++) {
        for (int sq = 0; sq < 64; sq++) {
            mg_table[make_piece(WHITE, pt)][sq] = mg_value[pt] + mg_pesto[pt][sq ^ 56];
            eg_table[make_piece(WHITE, pt)][sq] = eg_value[pt] + eg_pesto[pt][sq ^ 56];
            mg_table[make_piece(BLACK, pt)][sq] = mg_value[pt] + mg_pesto[pt][sq];
            eg_table[make_piece(BLACK, pt)][sq] = eg_value[pt] + eg_pesto[pt][sq];
        }
    }
}

int evaluate(const Position& pos) {
    int mg[2] = {0, 0}, eg[2] = {0, 0};
    int phase = 0;

    for (int p = 0; p < 12; p++) {
        Bitboard b = pos.pieces[p];
        phase += phase_inc[type_of(p)] * popcount(b);
        const int c = color_of(p);
        while (b) {
            int sq = pop_lsb(b);
            mg[c] += mg_table[p][sq];
            eg[c] += eg_table[p][sq];
        }
    }

    const Bitboard all = pos.occ[2];
    for (int c = WHITE; c <= BLACK; c++) {
        const int them = c ^ 1;
        const Bitboard my_pawns = pos.pieces[make_piece(c, PAWN)];
        const Bitboard their_pawns = pos.pieces[make_piece(them, PAWN)];
        const Bitboard mobility_area = ~pos.occ[c] & ~pawn_attacks_bb(their_pawns, them);

        // Pawn structure
        Bitboard b = my_pawns;
        while (b) {
            int sq = pop_lsb(b);
            int rr = c == WHITE ? rank_of(sq) : 7 - rank_of(sq);
            if (!(PassedMask[c][sq] & their_pawns)) {
                mg[c] += passed_mg[rr];
                eg[c] += passed_eg[rr];
            }
            if (!(AdjacentFiles[file_of(sq)] & my_pawns)) {
                mg[c] -= 8;
                eg[c] -= 12;
            }
        }
        for (int f = 0; f < 8; f++) {
            int n = popcount(my_pawns & FileBB[f]);
            if (n > 1) {
                mg[c] -= 8 * (n - 1);
                eg[c] -= 20 * (n - 1);
            }
        }

        if (popcount(pos.pieces[make_piece(c, BISHOP)]) >= 2) {
            mg[c] += 30;
            eg[c] += 50;
        }

        // Mobility
        b = pos.pieces[make_piece(c, KNIGHT)];
        while (b) {
            int mob = popcount(KnightAttacks[pop_lsb(b)] & mobility_area) - 4;
            mg[c] += 4 * mob;
            eg[c] += 4 * mob;
        }
        b = pos.pieces[make_piece(c, BISHOP)];
        while (b) {
            int mob = popcount(bishop_attacks(pop_lsb(b), all) & mobility_area) - 6;
            mg[c] += 5 * mob;
            eg[c] += 5 * mob;
        }
        b = pos.pieces[make_piece(c, ROOK)];
        while (b) {
            int sq = pop_lsb(b);
            int mob = popcount(rook_attacks(sq, all) & mobility_area) - 7;
            mg[c] += 2 * mob;
            eg[c] += 4 * mob;
            Bitboard file = FileBB[file_of(sq)];
            if (!(file & (my_pawns | their_pawns))) {
                mg[c] += 20;
                eg[c] += 5;
            } else if (!(file & my_pawns)) {
                mg[c] += 10;
            }
        }
        b = pos.pieces[make_piece(c, QUEEN)];
        while (b) {
            int mob = popcount(queen_attacks(pop_lsb(b), all) & mobility_area) - 13;
            mg[c] += mob;
            eg[c] += 2 * mob;
        }

        // Pawn shield in front of a castled king
        int ksq = pos.king_sq(c);
        int kr = c == WHITE ? rank_of(ksq) : 7 - rank_of(ksq);
        if (kr <= 1) {
            Bitboard ranks = 0;
            for (int i = 1; i <= 2; i++) {
                int r = c == WHITE ? rank_of(ksq) + i : rank_of(ksq) - i;
                if (r >= 0 && r < 8) ranks |= RANK_1_BB << (8 * r);
            }
            mg[c] += 12 * std::min(popcount(PassedMask[c][ksq] & ranks & my_pawns), 3);
        }
    }

    const int p = std::min(phase, 24);
    int score = ((mg[WHITE] - mg[BLACK]) * p + (eg[WHITE] - eg[BLACK]) * (24 - p)) / 24;

    // Mop-up: with no pawns left, help the stronger side drive the king to the edge.
    if (!(pos.pieces[WP] | pos.pieces[BP]) && std::abs(score) > 300) {
        int strong = score > 0 ? WHITE : BLACK;
        int sk = pos.king_sq(strong), wk = pos.king_sq(strong ^ 1);
        int kd = std::abs(file_of(sk) - file_of(wk)) + std::abs(rank_of(sk) - rank_of(wk));
        int bonus = 10 * center_distance(wk) + 4 * (14 - kd);
        score += strong == WHITE ? bonus : -bonus;
    }

    return (pos.side == WHITE ? score : -score) + 10; // tempo
}

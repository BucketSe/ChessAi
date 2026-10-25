#pragma once
#include <cstdint>

using U64 = uint64_t;
using Bitboard = uint64_t;
using Move = uint16_t; // bits 0-5 from, 6-11 to, 12-15 flag

enum Color : int { WHITE, BLACK };
enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum Piece : int { WP, WN, WB, WR, WQ, WK, BP, BN, BB, BR, BQ, BK, NO_PIECE };

// clang-format off
enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQ
};
// clang-format on

enum MoveFlag : int {
    QUIET = 0,
    DOUBLE_PUSH = 1,
    KING_CASTLE = 2,
    QUEEN_CASTLE = 3,
    CAPTURE = 4,
    EP_CAPTURE = 5,
    PR_KNIGHT = 8,
    PR_BISHOP = 9,
    PR_ROOK = 10,
    PR_QUEEN = 11,
    PC_KNIGHT = 12,
    PC_BISHOP = 13,
    PC_ROOK = 14,
    PC_QUEEN = 15,
};

constexpr int MAX_PLY = 128;
constexpr int MAX_MOVES = 256;
constexpr int INF = 32001;
constexpr int MATE = 32000;
constexpr int MATE_BOUND = MATE - MAX_PLY;

constexpr int make_piece(int c, int pt) { return c * 6 + pt; }
constexpr int type_of(int p) { return p % 6; }
constexpr int color_of(int p) { return p / 6; }

constexpr Move make_move(int from, int to, int flag) { return Move(from | (to << 6) | (flag << 12)); }
constexpr int from_sq(Move m) { return m & 63; }
constexpr int to_sq(Move m) { return (m >> 6) & 63; }
constexpr int flag_of(Move m) { return m >> 12; }
constexpr bool is_capture(Move m) { return (m >> 12) & 4; }
constexpr bool is_promo(Move m) { return (m >> 12) & 8; }
constexpr int promo_type(Move m) { return ((m >> 12) & 3) + KNIGHT; }

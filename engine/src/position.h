#pragma once
#include "bitboard.h"
#include <string>
#include <vector>

constexpr int WK_CASTLE = 1, WQ_CASTLE = 2, BK_CASTLE = 4, BQ_CASTLE = 8;
constexpr int MAX_GAME_PLY = 4096;

inline const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

namespace Zobrist {
extern U64 psq[12][64], side, castling[16], ep[8];
}

struct Undo {
    int captured;
    int castling;
    int ep;
    int halfmove;
    U64 hash;
};

class Position {
  public:
    Bitboard pieces[12];
    Bitboard occ[3]; // white, black, both
    int board[64];
    int side;
    int ep;
    int castling;
    int halfmove;
    int fullmove;
    U64 hash;
    int game_ply = 0;

    Position();
    void set_fen(const std::string& fen);
    std::string fen() const;
    void print() const;

    // Returns false (and leaves the position unchanged) if the move is illegal.
    bool make_move(Move m);
    void unmake_move(Move m);
    void make_null();
    void unmake_null();

    bool attacked(int sq, int by) const;
    Bitboard attackers_to(int sq, Bitboard occupied) const;
    int king_sq(int c) const { return lsb(pieces[make_piece(c, KING)]); }
    bool in_check() const { return attacked(king_sq(side), side ^ 1); }
    bool is_repetition() const;
    bool insufficient_material() const;
    bool has_non_pawn_material(int c) const {
        return pieces[make_piece(c, KNIGHT)] | pieces[make_piece(c, BISHOP)] | pieces[make_piece(c, ROOK)] |
               pieces[make_piece(c, QUEEN)];
    }

    // Parses a UCI move string ("e2e4", "e7e8q"); returns 0 if it is not legal here.
    Move parse_move(const std::string& s);
    U64 compute_hash() const;

  private:
    std::vector<Undo> undo;
    std::vector<U64> history;

    void put_piece(int p, int sq);
    void remove_piece(int sq);
    void move_piece(int from, int to);
};

std::string move_to_uci(Move m);
std::string square_name(int sq);
void init_position();

#pragma once
#include "types.h"
#include <bit>

constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;
constexpr Bitboard RANK_1_BB = 0xFFULL;
constexpr Bitboard RANK_2_BB = RANK_1_BB << 8;
constexpr Bitboard RANK_3_BB = RANK_1_BB << 16;
constexpr Bitboard RANK_6_BB = RANK_1_BB << 40;
constexpr Bitboard RANK_7_BB = RANK_1_BB << 48;
constexpr Bitboard RANK_8_BB = RANK_1_BB << 56;

constexpr Bitboard bit(int s) { return 1ULL << s; }
constexpr int rank_of(int s) { return s >> 3; }
constexpr int file_of(int s) { return s & 7; }

inline int popcount(Bitboard b) { return std::popcount(b); }
inline int lsb(Bitboard b) { return std::countr_zero(b); }
inline int pop_lsb(Bitboard& b) {
    int s = lsb(b);
    b &= b - 1;
    return s;
}

struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    int shift;
};

extern Magic RookMagics[64], BishopMagics[64];
extern Bitboard PawnAttacks[2][64], KnightAttacks[64], KingAttacks[64];
extern Bitboard FileBB[8], AdjacentFiles[8], PassedMask[2][64];

inline Bitboard rook_attacks(int sq, Bitboard occ) {
    const Magic& m = RookMagics[sq];
    return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
}
inline Bitboard bishop_attacks(int sq, Bitboard occ) {
    const Magic& m = BishopMagics[sq];
    return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
}
inline Bitboard queen_attacks(int sq, Bitboard occ) { return rook_attacks(sq, occ) | bishop_attacks(sq, occ); }

void init_bitboards();

#include "bitboard.h"

Magic RookMagics[64], BishopMagics[64];
Bitboard PawnAttacks[2][64], KnightAttacks[64], KingAttacks[64];
Bitboard FileBB[8], AdjacentFiles[8], PassedMask[2][64];

static Bitboard RookTable[102400];
static Bitboard BishopTable[5248];

namespace {

struct PRNG {
    U64 s;
    explicit PRNG(U64 seed) : s(seed) {}
    U64 next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    U64 sparse() { return next() & next() & next(); }
};

// {rank delta, file delta}
const int RookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
const int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
const int KnightSteps[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
const int KingSteps[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

bool on_board(int r, int f) { return r >= 0 && r < 8 && f >= 0 && f < 8; }

Bitboard slide(int sq, Bitboard occ, const int dirs[4][2]) {
    Bitboard att = 0;
    const int r = rank_of(sq), f = file_of(sq);
    for (int d = 0; d < 4; d++) {
        for (int rr = r + dirs[d][0], ff = f + dirs[d][1]; on_board(rr, ff); rr += dirs[d][0], ff += dirs[d][1]) {
            att |= bit(rr * 8 + ff);
            if (occ & bit(rr * 8 + ff)) break;
        }
    }
    return att;
}

Bitboard leaper(int sq, const int steps[][2], int n) {
    Bitboard att = 0;
    for (int i = 0; i < n; i++) {
        int r = rank_of(sq) + steps[i][0], f = file_of(sq) + steps[i][1];
        if (on_board(r, f)) att |= bit(r * 8 + f);
    }
    return att;
}

// Finds magic numbers at startup (takes a few milliseconds) using the
// "fancy magic" scheme: each square gets its own slice of a shared table.
void init_magics(Magic* magics, Bitboard* table, const int dirs[4][2]) {
    static Bitboard occs[4096], refs[4096];
    static int epoch[4096];
    int attempt = 0;
    PRNG rng(1070372);
    Bitboard* ptr = table;

    for (int sq = 0; sq < 64; sq++) {
        Bitboard edges = ((RANK_1_BB | RANK_8_BB) & ~(RANK_1_BB << (8 * rank_of(sq)))) |
                         ((FILE_A_BB | FILE_H_BB) & ~(FILE_A_BB << file_of(sq)));
        Magic& m = magics[sq];
        m.mask = slide(sq, 0, dirs) & ~edges;
        m.shift = 64 - popcount(m.mask);
        m.attacks = ptr;

        int size = 0;
        Bitboard b = 0;
        do { // enumerate all subsets of the mask (Carry-Rippler)
            occs[size] = b;
            refs[size] = slide(sq, b, dirs);
            size++;
            b = (b - m.mask) & m.mask;
        } while (b);

        for (;;) {
            m.magic = rng.sparse();
            if (popcount((m.mask * m.magic) >> 56) < 6) continue;
            attempt++;
            int i = 0;
            for (; i < size; i++) {
                unsigned idx = unsigned((occs[i] * m.magic) >> m.shift);
                if (epoch[idx] < attempt) {
                    epoch[idx] = attempt;
                    ptr[idx] = refs[i];
                } else if (ptr[idx] != refs[i]) {
                    break;
                }
            }
            if (i == size) break;
        }
        ptr += size;
    }
}

} // namespace

void init_bitboards() {
    for (int f = 0; f < 8; f++) FileBB[f] = FILE_A_BB << f;
    for (int f = 0; f < 8; f++)
        AdjacentFiles[f] = (f > 0 ? FileBB[f - 1] : 0) | (f < 7 ? FileBB[f + 1] : 0);

    for (int sq = 0; sq < 64; sq++) {
        const int r = rank_of(sq), f = file_of(sq);
        KnightAttacks[sq] = leaper(sq, KnightSteps, 8);
        KingAttacks[sq] = leaper(sq, KingSteps, 8);
        const int wp[2][2] = {{1, -1}, {1, 1}}, bp[2][2] = {{-1, -1}, {-1, 1}};
        PawnAttacks[WHITE][sq] = leaper(sq, wp, 2);
        PawnAttacks[BLACK][sq] = leaper(sq, bp, 2);

        PassedMask[WHITE][sq] = PassedMask[BLACK][sq] = 0;
        Bitboard files = FileBB[f] | AdjacentFiles[f];
        for (int rr = r + 1; rr < 8; rr++) PassedMask[WHITE][sq] |= files & (RANK_1_BB << (8 * rr));
        for (int rr = r - 1; rr >= 0; rr--) PassedMask[BLACK][sq] |= files & (RANK_1_BB << (8 * rr));
    }

    init_magics(RookMagics, RookTable, RookDirs);
    init_magics(BishopMagics, BishopTable, BishopDirs);
}

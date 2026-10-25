#include "movegen.h"

static void add_promotions(MoveList& list, int from, int to, bool capture, bool queen_only) {
    const int base = capture ? PC_KNIGHT : PR_KNIGHT;
    list.add(make_move(from, to, base + 3)); // queen first
    if (queen_only) return;
    list.add(make_move(from, to, base + 0));
    list.add(make_move(from, to, base + 2));
    list.add(make_move(from, to, base + 1));
}

void generate_moves(const Position& pos, MoveList& list, bool captures_only) {
    const int us = pos.side, them = us ^ 1;
    const Bitboard own = pos.occ[us], enemy = pos.occ[them], all = pos.occ[2], empty = ~all;
    const Bitboard pawns = pos.pieces[make_piece(us, PAWN)];
    const Bitboard promo_rank = us == WHITE ? RANK_8_BB : RANK_1_BB;
    const int up = us == WHITE ? 8 : -8;

    // Pawn pushes
    Bitboard single = (us == WHITE ? pawns << 8 : pawns >> 8) & empty;
    Bitboard dbl = (us == WHITE ? (single & RANK_3_BB) << 8 : (single & RANK_6_BB) >> 8) & empty;

    Bitboard b = single & promo_rank;
    while (b) {
        int to = pop_lsb(b);
        add_promotions(list, to - up, to, false, captures_only);
    }
    if (!captures_only) {
        b = single & ~promo_rank;
        while (b) {
            int to = pop_lsb(b);
            list.add(make_move(to - up, to, QUIET));
        }
        while (dbl) {
            int to = pop_lsb(dbl);
            list.add(make_move(to - 2 * up, to, DOUBLE_PUSH));
        }
    }

    // Pawn captures
    b = pawns;
    while (b) {
        int from = pop_lsb(b);
        Bitboard att = PawnAttacks[us][from] & enemy;
        while (att) {
            int to = pop_lsb(att);
            if (bit(to) & promo_rank) add_promotions(list, from, to, true, false);
            else list.add(make_move(from, to, CAPTURE));
        }
    }
    if (pos.ep != NO_SQ) {
        Bitboard att = PawnAttacks[them][pos.ep] & pawns;
        while (att) list.add(make_move(pop_lsb(att), pos.ep, EP_CAPTURE));
    }

    // Pieces
    const Bitboard targets = captures_only ? enemy : ~own;
    for (int pt = KNIGHT; pt <= KING; pt++) {
        Bitboard pcs = pos.pieces[make_piece(us, pt)];
        while (pcs) {
            int from = pop_lsb(pcs);
            Bitboard att;
            switch (pt) {
            case KNIGHT: att = KnightAttacks[from]; break;
            case BISHOP: att = bishop_attacks(from, all); break;
            case ROOK: att = rook_attacks(from, all); break;
            case QUEEN: att = queen_attacks(from, all); break;
            default: att = KingAttacks[from]; break;
            }
            att &= targets;
            while (att) {
                int to = pop_lsb(att);
                list.add(make_move(from, to, (bit(to) & enemy) ? CAPTURE : QUIET));
            }
        }
    }

    // Castling (the destination square is verified by make_move's legality check)
    if (captures_only) return;
    if (us == WHITE) {
        if ((pos.castling & WK_CASTLE) && !(all & (bit(F1) | bit(G1))) && !pos.attacked(E1, BLACK) &&
            !pos.attacked(F1, BLACK))
            list.add(make_move(E1, G1, KING_CASTLE));
        if ((pos.castling & WQ_CASTLE) && !(all & (bit(B1) | bit(C1) | bit(D1))) && !pos.attacked(E1, BLACK) &&
            !pos.attacked(D1, BLACK))
            list.add(make_move(E1, C1, QUEEN_CASTLE));
    } else {
        if ((pos.castling & BK_CASTLE) && !(all & (bit(F8) | bit(G8))) && !pos.attacked(E8, WHITE) &&
            !pos.attacked(F8, WHITE))
            list.add(make_move(E8, G8, KING_CASTLE));
        if ((pos.castling & BQ_CASTLE) && !(all & (bit(B8) | bit(C8) | bit(D8))) && !pos.attacked(E8, WHITE) &&
            !pos.attacked(D8, WHITE))
            list.add(make_move(E8, C8, QUEEN_CASTLE));
    }
}

U64 perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    MoveList list;
    generate_moves(pos, list);
    U64 nodes = 0;
    for (int i = 0; i < list.count; i++) {
        if (!pos.make_move(list.moves[i])) continue;
        nodes += perft(pos, depth - 1);
        pos.unmake_move(list.moves[i]);
    }
    return nodes;
}

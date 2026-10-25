#include "position.h"
#include "movegen.h"
#include <cstdio>
#include <cstring>
#include <sstream>

namespace Zobrist {
U64 psq[12][64], side, castling[16], ep[8];
}

static int castle_mask[64];
static const char PIECE_CHARS[] = "PNBRQKpnbrqk";

void init_position() {
    U64 s = 0x9E3779B97F4A7C15ULL;
    auto next = [&] {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    };
    for (auto& p : Zobrist::psq)
        for (auto& sq : p) sq = next();
    Zobrist::side = next();
    for (auto& c : Zobrist::castling) c = next();
    for (auto& e : Zobrist::ep) e = next();

    for (int& m : castle_mask) m = 15;
    castle_mask[A1] = 15 & ~WQ_CASTLE;
    castle_mask[E1] = 15 & ~(WK_CASTLE | WQ_CASTLE);
    castle_mask[H1] = 15 & ~WK_CASTLE;
    castle_mask[A8] = 15 & ~BQ_CASTLE;
    castle_mask[E8] = 15 & ~(BK_CASTLE | BQ_CASTLE);
    castle_mask[H8] = 15 & ~BK_CASTLE;
}

std::string square_name(int sq) {
    return {char('a' + file_of(sq)), char('1' + rank_of(sq))};
}

std::string move_to_uci(Move m) {
    if (!m) return "0000";
    std::string s = square_name(from_sq(m)) + square_name(to_sq(m));
    if (is_promo(m)) s += "nbrq"[promo_type(m) - KNIGHT];
    return s;
}

Position::Position() : undo(MAX_GAME_PLY), history(MAX_GAME_PLY) { set_fen(START_FEN); }

void Position::put_piece(int p, int sq) {
    Bitboard b = bit(sq);
    pieces[p] |= b;
    occ[color_of(p)] |= b;
    occ[2] |= b;
    board[sq] = p;
    hash ^= Zobrist::psq[p][sq];
}

void Position::remove_piece(int sq) {
    int p = board[sq];
    Bitboard b = bit(sq);
    pieces[p] ^= b;
    occ[color_of(p)] ^= b;
    occ[2] ^= b;
    board[sq] = NO_PIECE;
    hash ^= Zobrist::psq[p][sq];
}

void Position::move_piece(int from, int to) {
    int p = board[from];
    Bitboard ft = bit(from) | bit(to);
    pieces[p] ^= ft;
    occ[color_of(p)] ^= ft;
    occ[2] ^= ft;
    board[to] = p;
    board[from] = NO_PIECE;
    hash ^= Zobrist::psq[p][from] ^ Zobrist::psq[p][to];
}

U64 Position::compute_hash() const {
    U64 h = 0;
    for (int sq = 0; sq < 64; sq++)
        if (board[sq] != NO_PIECE) h ^= Zobrist::psq[board[sq]][sq];
    if (side == BLACK) h ^= Zobrist::side;
    h ^= Zobrist::castling[castling];
    if (ep != NO_SQ) h ^= Zobrist::ep[file_of(ep)];
    return h;
}

void Position::set_fen(const std::string& fen) {
    std::memset(pieces, 0, sizeof(pieces));
    std::memset(occ, 0, sizeof(occ));
    for (int& p : board) p = NO_PIECE;
    hash = 0;

    std::istringstream ss(fen);
    std::string placement, stm, cast, eps;
    ss >> placement >> stm >> cast >> eps;
    if (!(ss >> halfmove)) halfmove = 0;
    if (!(ss >> fullmove)) fullmove = 1;

    int r = 7, f = 0;
    for (char ch : placement) {
        if (ch == '/') {
            r--;
            f = 0;
        } else if (ch >= '1' && ch <= '8') {
            f += ch - '0';
        } else if (const char* p = std::strchr(PIECE_CHARS, ch); p && r >= 0 && f < 8) {
            put_piece(int(p - PIECE_CHARS), r * 8 + f);
            f++;
        }
    }

    side = stm == "b" ? BLACK : WHITE;
    castling = 0;
    for (char ch : cast) {
        if (ch == 'K') castling |= WK_CASTLE;
        if (ch == 'Q') castling |= WQ_CASTLE;
        if (ch == 'k') castling |= BK_CASTLE;
        if (ch == 'q') castling |= BQ_CASTLE;
    }
    ep = (eps.size() == 2) ? (eps[0] - 'a') + (eps[1] - '1') * 8 : NO_SQ;
    hash = compute_hash();
    game_ply = 0;
}

std::string Position::fen() const {
    std::string s;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int p = board[r * 8 + f];
            if (p == NO_PIECE) {
                empty++;
                continue;
            }
            if (empty) s += char('0' + empty), empty = 0;
            s += PIECE_CHARS[p];
        }
        if (empty) s += char('0' + empty);
        if (r) s += '/';
    }
    s += side == WHITE ? " w " : " b ";
    if (!castling) s += '-';
    if (castling & WK_CASTLE) s += 'K';
    if (castling & WQ_CASTLE) s += 'Q';
    if (castling & BK_CASTLE) s += 'k';
    if (castling & BQ_CASTLE) s += 'q';
    s += ' ';
    s += ep == NO_SQ ? "-" : square_name(ep);
    s += " " + std::to_string(halfmove) + " " + std::to_string(fullmove);
    return s;
}

void Position::print() const {
    std::string out = "\n +---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; r--) {
        for (int f = 0; f < 8; f++) {
            int p = board[r * 8 + f];
            out += " | ";
            out += p == NO_PIECE ? ' ' : PIECE_CHARS[p];
        }
        out += " | " + std::to_string(r + 1) + "\n +---+---+---+---+---+---+---+---+\n";
    }
    out += "   a   b   c   d   e   f   g   h\n\nFen: " + fen() + "\n";
    char key[32];
    std::snprintf(key, sizeof(key), "%016llX", (unsigned long long)hash);
    out += "Key: " + std::string(key) + "\n";
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
}

bool Position::attacked(int sq, int by) const {
    if (PawnAttacks[by ^ 1][sq] & pieces[make_piece(by, PAWN)]) return true;
    if (KnightAttacks[sq] & pieces[make_piece(by, KNIGHT)]) return true;
    if (KingAttacks[sq] & pieces[make_piece(by, KING)]) return true;
    Bitboard queens = pieces[make_piece(by, QUEEN)];
    if (bishop_attacks(sq, occ[2]) & (pieces[make_piece(by, BISHOP)] | queens)) return true;
    if (rook_attacks(sq, occ[2]) & (pieces[make_piece(by, ROOK)] | queens)) return true;
    return false;
}

Bitboard Position::attackers_to(int sq, Bitboard o) const {
    return (PawnAttacks[BLACK][sq] & pieces[WP]) | (PawnAttacks[WHITE][sq] & pieces[BP]) |
           (KnightAttacks[sq] & (pieces[WN] | pieces[BN])) | (KingAttacks[sq] & (pieces[WK] | pieces[BK])) |
           (bishop_attacks(sq, o) & (pieces[WB] | pieces[BB] | pieces[WQ] | pieces[BQ])) |
           (rook_attacks(sq, o) & (pieces[WR] | pieces[BR] | pieces[WQ] | pieces[BQ]));
}

bool Position::make_move(Move m) {
    const int from = from_sq(m), to = to_sq(m), flag = flag_of(m);
    const int us = side, them = us ^ 1;
    const int pc = board[from];

    Undo& u = undo[game_ply];
    u.captured = NO_PIECE;
    u.castling = castling;
    u.ep = ep;
    u.halfmove = halfmove;
    u.hash = hash;
    history[game_ply] = hash;
    game_ply++;

    if (ep != NO_SQ) hash ^= Zobrist::ep[file_of(ep)];
    ep = NO_SQ;
    halfmove++;

    if (flag == EP_CAPTURE) {
        int capsq = to + (us == WHITE ? -8 : 8);
        u.captured = board[capsq];
        remove_piece(capsq);
    } else if (is_capture(m)) {
        u.captured = board[to];
        remove_piece(to);
    }
    if (u.captured != NO_PIECE || type_of(pc) == PAWN) halfmove = 0;

    move_piece(from, to);

    if (is_promo(m)) {
        remove_piece(to);
        put_piece(make_piece(us, promo_type(m)), to);
    } else if (flag == DOUBLE_PUSH) {
        ep = from + (us == WHITE ? 8 : -8);
        hash ^= Zobrist::ep[file_of(ep)];
    } else if (flag == KING_CASTLE) {
        if (us == WHITE) move_piece(H1, F1);
        else move_piece(H8, F8);
    } else if (flag == QUEEN_CASTLE) {
        if (us == WHITE) move_piece(A1, D1);
        else move_piece(A8, D8);
    }

    hash ^= Zobrist::castling[castling];
    castling &= castle_mask[from] & castle_mask[to];
    hash ^= Zobrist::castling[castling];

    side = them;
    hash ^= Zobrist::side;
    if (us == BLACK) fullmove++;

    if (attacked(king_sq(us), them)) {
        unmake_move(m);
        return false;
    }
    return true;
}

void Position::unmake_move(Move m) {
    game_ply--;
    const Undo& u = undo[game_ply];
    const int from = from_sq(m), to = to_sq(m), flag = flag_of(m);
    side ^= 1;
    const int us = side;
    if (us == BLACK) fullmove--;

    if (is_promo(m)) {
        remove_piece(to);
        put_piece(make_piece(us, PAWN), to);
    }
    move_piece(to, from);

    if (flag == KING_CASTLE) {
        if (us == WHITE) move_piece(F1, H1);
        else move_piece(F8, H8);
    } else if (flag == QUEEN_CASTLE) {
        if (us == WHITE) move_piece(D1, A1);
        else move_piece(D8, A8);
    }

    if (u.captured != NO_PIECE) {
        int capsq = flag == EP_CAPTURE ? to + (us == WHITE ? -8 : 8) : to;
        put_piece(u.captured, capsq);
    }

    castling = u.castling;
    ep = u.ep;
    halfmove = u.halfmove;
    hash = u.hash;
}

void Position::make_null() {
    Undo& u = undo[game_ply];
    u.captured = NO_PIECE;
    u.castling = castling;
    u.ep = ep;
    u.halfmove = halfmove;
    u.hash = hash;
    history[game_ply] = hash;
    game_ply++;

    if (ep != NO_SQ) hash ^= Zobrist::ep[file_of(ep)];
    ep = NO_SQ;
    halfmove = 0; // a null move breaks repetition chains
    side ^= 1;
    hash ^= Zobrist::side;
}

void Position::unmake_null() {
    game_ply--;
    const Undo& u = undo[game_ply];
    side ^= 1;
    ep = u.ep;
    halfmove = u.halfmove;
    hash = u.hash;
}

bool Position::is_repetition() const {
    for (int i = game_ply - 4; i >= 0 && i >= game_ply - halfmove; i -= 2)
        if (history[i] == hash) return true;
    return false;
}

bool Position::insufficient_material() const {
    if (pieces[WP] | pieces[BP] | pieces[WR] | pieces[BR] | pieces[WQ] | pieces[BQ]) return false;
    return popcount(pieces[WN] | pieces[WB]) <= 1 && popcount(pieces[BN] | pieces[BB]) <= 1;
}

Move Position::parse_move(const std::string& s) {
    MoveList list;
    generate_moves(*this, list);
    for (int i = 0; i < list.count; i++) {
        Move m = list.moves[i];
        if (move_to_uci(m) != s) continue;
        if (make_move(m)) {
            unmake_move(m);
            return m;
        }
    }
    return 0;
}

#pragma once
#include "types.h"
#include <cstddef>
#include <vector>

enum TTFlag : uint8_t { TT_NONE, TT_EXACT, TT_LOWER, TT_UPPER };

struct TTEntry {
    U64 key;
    Move move;
    int16_t score;
    int8_t depth;
    uint8_t flag;
    uint8_t age;
};

class TranspositionTable {
  public:
    void resize(size_t mb);
    void clear();
    void new_search() { age++; }
    const TTEntry* probe(U64 key) const {
        const TTEntry& e = table[key & mask];
        return e.key == key && e.flag != TT_NONE ? &e : nullptr;
    }
    void store(U64 key, Move move, int score, int depth, int flag);
    int hashfull() const; // permille of entries used by the current search

  private:
    std::vector<TTEntry> table;
    size_t mask = 0;
    uint8_t age = 0;
};

extern TranspositionTable TT;

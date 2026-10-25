#include "tt.h"
#include <algorithm>

TranspositionTable TT;

void TranspositionTable::resize(size_t mb) {
    size_t count = 1;
    while (count * 2 * sizeof(TTEntry) <= mb * 1024 * 1024) count *= 2;
    table.assign(count, TTEntry{});
    mask = count - 1;
}

void TranspositionTable::clear() {
    std::fill(table.begin(), table.end(), TTEntry{});
    age = 0;
}

void TranspositionTable::store(U64 key, Move move, int score, int depth, int flag) {
    TTEntry& e = table[key & mask];
    if (e.key == key && depth < e.depth - 2 && flag != TT_EXACT && e.age == age) return;
    if (move || e.key != key) e.move = move; // keep the old best move if we have none
    e.key = key;
    e.score = int16_t(score);
    e.depth = int8_t(depth);
    e.flag = uint8_t(flag);
    e.age = age;
}

int TranspositionTable::hashfull() const {
    int used = 0;
    for (size_t i = 0; i < 1000 && i < table.size(); i++)
        if (table[i].flag != TT_NONE && table[i].age == age) used++;
    return used;
}

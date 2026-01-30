#include "HashTable.h"
#include "TopN.h"
#include <cstring>
#include <iostream>

// ---- helpers ----
bool CompareCString(const char* a, const char* b) {
    return std::strcmp(a, b) == 0;
}

uint64_t HashString(const char* s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= static_cast<unsigned char>(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

// ---- test ----
int main() {
    HashTable<const char*, size_t> table(
        CompareCString,
        HashString,
        10
    );

    table.Find("apple", 3);
    table.Find("banana", 7);
    table.Find("cherry", 5);
    table.Find("date", 9);

    Pair** top = TopN(&table, 3);

    for (int i = 0; i < 3; ++i) {
        if (top[i]) {
            std::cout << top[i]->key << " : " << top[i]->value << "\n";
        }
    }

    delete[] top;
}


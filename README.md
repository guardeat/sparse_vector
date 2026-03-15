# sparse_vector

A C++ sparse vector with O(1) insertion, erasure, and stable index reuse. Elements are stored in a flat array; erased slots are tracked with a bitset-based free list and reused on the next insertion.

## Features

- **Stable indices** — erased slot indices are reused, existing indices never shift
- **O(1) insert and erase** — free slot found via `std::countr_zero` on a bitset chunk
- **Cache-friendly iteration** — iterates only live elements, skipping erased slots using bitset masks
- **Standard allocator support** — works with any `std::allocator`-compatible allocator
- **Automatic growth** — doubles capacity when full, capacity always aligned to chunk size (64)

## Requirements

- C++20 or later
- Standard library headers: `<iterator>`, `<map>`, `<bitset>`, `<bit>`, `<concepts>`

## Usage
```cpp
#include "sparse_vector.hpp"

byte::sparse_vector<int> sv;

// insert — returns stable index
size_t i0 = sv.push(10);
size_t i1 = sv.push(20);
size_t i2 = sv.push(30);

// access by index
sv[i0];       // 10
sv.at(i1);    // 20

// erase — slot is marked free, index invalidated
sv.erase(i1);

// next push reuses the freed slot
size_t i3 = sv.push(99);  // i3 == i1

// iterate over live elements only
for (auto& val : sv) {
    // skips erased slots automatically
}

// iterate with index
for (auto it = sv.begin(); it != sv.end(); ++it) {
    size_t index = it.index();
    auto&  value = *it;
}
```

## API

| Method | Description |
|---|---|
| `push(value)` | Insert by copy or move, returns index |
| `emplace(args...)` | Construct in place, returns index |
| `erase(index)` | Destroy element, mark slot free |
| `at(index)` | Bounds-checked access |
| `operator[](index)` | Unchecked access |
| `reserve(n)` | Pre-allocate capacity |
| `clear()` | Destroy all elements, reset |
| `size()` | Number of live elements |
| `begin() / end()` | Iterator over live elements |

## Design Notes

**Free list via bitset chunks** — capacity is divided into 64-element chunks. Each chunk has a `std::bitset<64>` tracking which slots are occupied. A `std::map<size_t, bitset>` stores only chunks that have free slots — when a chunk is full it is removed from the map. Insertion finds the lowest free slot via `countr_zero(~chunk)`.

**Iteration** — the iterator caches the current chunk's bitset and uses bit masking to skip to the next live element without checking each slot individually.

**Index stability** — unlike `std::vector`, erasing an element never moves others. Indices returned by `push`/`emplace` remain valid until that specific index is erased.

## Limitations

- Random access by index is unchecked — accessing an erased index is undefined behavior

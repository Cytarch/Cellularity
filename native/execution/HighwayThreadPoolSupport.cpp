#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "hwy/contrib/sort/vqsort.h"

// Highway's ThreadPool autotuner uses VQSort only for a fixed buffer of at
// most fourteen scalar costs. Linking all of hwy_contrib solely for that call
// adds unrelated SIMD sort implementations to the application. These exact
// overloads retain the required ordering semantics with the standard scalar
// sort and are private to Cellularity's focused execution support library.
namespace hwy {

void VQSort(double* HWY_RESTRICT keys, std::size_t count, SortAscending) {
    std::sort(keys, keys + count);
}

void VQSort(std::uint64_t* HWY_RESTRICT keys, std::size_t count,
        SortAscending) {
    std::sort(keys, keys + count);
}

} // namespace hwy

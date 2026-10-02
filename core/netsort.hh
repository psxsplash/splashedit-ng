// List<T>.Sort(Comparison<T>) as the .NET Framework reference source
// implements it (ArraySortHelper.IntrospectiveSort). It is not stable, and the
// original exporter sorts several arrays on keys that tie, so reproducing its
// output needs the same algorithm, not just any sort.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace splash::netsort {

namespace detail {

inline int floorLog2(int n) {
    int result = 0;
    while (n >= 1) {
        result++;
        n /= 2;
    }
    return result;
}

template <typename T, typename Cmp>
void swapIfGreater(std::vector<T>& k, Cmp& cmp, int a, int b) {
    if (a != b && cmp(k[a], k[b]) > 0) std::swap(k[a], k[b]);
}

template <typename T, typename Cmp>
void insertionSort(std::vector<T>& k, int lo, int hi, Cmp& cmp) {
    for (int i = lo; i < hi; i++) {
        int j = i;
        T t = k[i + 1];
        while (j >= lo && cmp(t, k[j]) < 0) {
            k[j + 1] = k[j];
            j--;
        }
        k[j + 1] = t;
    }
}

template <typename T, typename Cmp>
void downHeap(std::vector<T>& k, int i, int n, int lo, Cmp& cmp) {
    T d = k[lo + i - 1];
    while (i <= n / 2) {
        int child = 2 * i;
        if (child < n && cmp(k[lo + child - 1], k[lo + child]) < 0) child++;
        if (!(cmp(d, k[lo + child - 1]) < 0)) break;
        k[lo + i - 1] = k[lo + child - 1];
        i = child;
    }
    k[lo + i - 1] = d;
}

template <typename T, typename Cmp>
void heapsort(std::vector<T>& k, int lo, int hi, Cmp& cmp) {
    int n = hi - lo + 1;
    for (int i = n / 2; i >= 1; i--) downHeap(k, i, n, lo, cmp);
    for (int i = n; i > 1; i--) {
        std::swap(k[lo], k[lo + i - 1]);
        downHeap(k, 1, i - 1, lo, cmp);
    }
}

template <typename T, typename Cmp>
int pickPivotAndPartition(std::vector<T>& k, int lo, int hi, Cmp& cmp) {
    int middle = lo + ((hi - lo) >> 1);
    swapIfGreater(k, cmp, lo, middle);
    swapIfGreater(k, cmp, lo, hi);
    swapIfGreater(k, cmp, middle, hi);
    T pivot = k[middle];
    std::swap(k[middle], k[hi - 1]);
    int left = lo, right = hi - 1;
    while (left < right) {
        while (cmp(k[++left], pivot) < 0) {
        }
        while (cmp(pivot, k[--right]) < 0) {
        }
        if (left >= right) break;
        std::swap(k[left], k[right]);
    }
    std::swap(k[left], k[hi - 1]);
    return left;
}

template <typename T, typename Cmp>
void introSort(std::vector<T>& k, int lo, int hi, int depthLimit, Cmp& cmp) {
    while (hi > lo) {
        int partitionSize = hi - lo + 1;
        if (partitionSize <= 16) {
            if (partitionSize == 1) return;
            if (partitionSize == 2) {
                swapIfGreater(k, cmp, lo, hi);
                return;
            }
            if (partitionSize == 3) {
                swapIfGreater(k, cmp, lo, hi - 1);
                swapIfGreater(k, cmp, lo, hi);
                swapIfGreater(k, cmp, hi - 1, hi);
                return;
            }
            insertionSort(k, lo, hi, cmp);
            return;
        }
        if (depthLimit == 0) {
            heapsort(k, lo, hi, cmp);
            return;
        }
        depthLimit--;
        int p = pickPivotAndPartition(k, lo, hi, cmp);
        introSort(k, p + 1, hi, depthLimit, cmp);
        hi = p - 1;
    }
}

}  // namespace detail

// cmp(a, b) returns <0, 0 or >0 like a .NET Comparison<T>. `capacity` is the
// backing array length of the List<T>, which the depth limit is derived from;
// it only matters once the sort falls back to heapsort.
template <typename T, typename Cmp>
void listSort(std::vector<T>& k, Cmp cmp, std::size_t capacity = 0) {
    int n = int(k.size());
    if (n < 2) return;
    int cap = capacity ? int(capacity) : n;
    detail::introSort(k, 0, n - 1, 2 * detail::floorLog2(cap), cmp);
}

// Capacity of a List<T> grown one Add at a time from empty.
inline std::size_t listCapacityAfterAdds(std::size_t count) {
    if (count == 0) return 0;
    std::size_t c = 4;
    while (c < count) c *= 2;
    return c;
}

}  // namespace splash::netsort

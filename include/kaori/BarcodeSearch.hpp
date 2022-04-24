#ifndef KAORI_VARIABLE_LIBRARY_HPP
#define KAORI_VARIABLE_LIBRARY_HPP

#include "BarcodePool.hpp"
#include "MismatchTrie.hpp"
#include "utils.hpp"
#include <unordered_map>
#include <string>
#include <vector>
#include <array>

/**
 * @file BarcodeSearch.hpp
 *
 * @brief Search for barcode sequences.
 */

namespace kaori {

/** 
 * @cond
 */
template<class Trie>
void fill_library(
    const std::vector<const char*>& options, 
    std::unordered_map<std::string, int>& exact,
    Trie& trie,
    bool reverse,
    bool duplicates
) {
    size_t len = trie.get_length();

    for (size_t i = 0; i < options.size(); ++i) {
        auto ptr = options[i];

        std::string current;
        if (!reverse) {
            current = std::string(ptr, ptr + len);
        } else {
            for (int j = 0; j < len; ++j) {
                current += reverse_complement(ptr[len - j - 1]);
            }
        }

        auto it = exact.find(current);
        if (exact.find(current) != exact.end()) {
            if (!duplicates) {
                throw std::runtime_error("duplicate variable sequence '" + current + "'");
            }
        } else {
            exact[current] = i;
        }

        // Note that this must be called, even if the sequence is duplicated;
        // otherwise the trie's internal counter will not be properly incremented.
        trie.add(current.c_str(), duplicates);
    }
    return;
}

template<class Indexer, class Updater, class Cache, class Trie, class Result, class Mismatch>
void matcher_in_the_rye(const std::string& x, const Cache& cache, const Trie& trie, Result& res, const Mismatch& mismatches, const Mismatch& max_mismatches) {
    // Seeing if it's any of the caches; otherwise searching the trie.
    auto cit = cache.find(x);
    if (cit == cache.end()) {
        auto lit = res.cache.find(x);
        if (lit != res.cache.end()) {
            Updater::update(res, lit->second);
        } else {
            auto missed = trie.search(x.c_str(), mismatches);

            // The trie search breaks early when it hits the mismatch cap,
            // but the cap might be different across calls. If we break
            // early and report a miss, the miss will be cached and
            // returned in cases where there is a higher cap (and thus
            // might actually be a hit). As such, we should only store a
            // miss in the cache when the requested number of mismatches is
            // equal to the maximum value specified in the constructor.
            if (Indexer::index(missed) >= 0 || mismatches == max_mismatches) {
                res.cache[x] = missed;
            }

            Updater::update(res, missed);
        }
    } else {
        Updater::update(res, cit->second);
    }
    return;
}
/** 
 * @endcond
 */

/**
 * @brief Search for known barcode sequences.
 *
 * This supports exact and mismatch-aware searches for known sequences.
 * Mismatches may be distributed anywhere along the length of the sequence, see `AnyMismatches` for details.
 * Instances of this class use caching to avoid redundant work when a mismatching sequence has been previously encountered.
 */
class SimpleBarcodeSearch {
public:
    /**
     * Default constructor.
     */
    SimpleBarcodeSearch() {}

    /**
     * @param barcode_pool Pool of barcode sequences.
     * @param max_mismatches Maximum number of mismatches for any search performed by this class.
     * @param reverse Whether to reverse-complement the barcode sequences.
     * @param duplicates Whether duplicated `sequences` in `barcode_pool` are supported, see `MismatchTrie`.
     */
    SimpleBarcodeSearch(const BarcodePool& barcode_pool, int max_mismatches = 0, bool reverse = false, bool duplicates = false) : 
        trie(barcode_pool.length), 
        max_mm(max_mismatches) 
    {
        fill_library(barcode_pool.pool, exact, trie, reverse, duplicates);
        return;
    }

public:
    /**
     * @brief State of the search.
     *
     * This contains both the results of the search for any given input sequence,
     * as well as cached mismatches to optimize searches for future inputs.
     */
    struct State {
        /**
         * Index of the known sequence that matches best to the input sequence in `search()` (i.e., fewest total mismatches).
         * If no match was found or if the best match is ambiguous, this will be set to -1.
         */
        int index = 0;

        /**
         * Number of mismatches with the matching known sequence.
         * This should only be used if `index != -1`.
         */
        int mismatches = 0;
        
        /**
         * @cond
         */
        std::unordered_map<std::string, std::pair<int, int> > cache;
        /**
         * @endcond
         */
    };

    /**
     * Initialize the search state for thread-safe execution.
     *
     * @return A new `SeachState()`.
     */
    State initialize() const {
        return State();
    }

    /**
     * Incorporate the mismatch cache from `state` into the cache for this `SimpleBarcodeSearch` instance.
     * This allows regular consolidation of optimizations across threads.
     *
     * @param state A state object generated by `initialize()`.
     * Typically this has already been used in `search()` at least once.
     *
     * @return The mismatch cache of `state` is combined with that of this instance.
     */
    void reduce(State& state) {
        cache.merge(state.cache);
        state.cache.clear();
    }

private:
    struct Index {
        static int index(const std::pair<int, int>& val) {
            return val.first;
        }
    };

    struct Updator {
        static void update(State& state, const std::pair<int, int>& val) {
            state.index = val.first;
            state.mismatches = val.second;
            return;
        }
    };

public:
    /**
     * Search the known sequences in the barcode pool for an input sequence.
     * The number of allowed mismatches is equal to the maximum specified in the constructor.
     * 
     * @param search_seq The input sequence to use for searching.
     * This is expected to have the same length as the known sequences.
     * @param state A state object generated by `initialize()`.
     *
     * @return `state` is filled with the details of the best-matching barcode sequence, if any exists.
     */
    void search(const std::string& search_seq, State& state) const {
        search(search_seq, state, max_mm);
        return;
    }

    /**
     * Search the known sequences in the barcode pool for an input sequence,
     * with potentially more stringent mismatch requirements.
     * This can improve efficiency in situations where some mismatches have already been consumed by matching the template sequence.
     *
     * @param search_seq The input sequence to use for searching.
     * This is expected to have the same length as the known sequences.
     * @param state A state object generated by `initialize()`.
     * @param allowed_mismatches Allowed number of mismatches.
     * This should not be greater than the maximum specified in the constructor.
     *
     * @return `state` is filled with the details of the best-matching barcode sequence, if any exists.
     */
    void search(const std::string& search_seq, State& state, int allowed_mismatches) const {
        auto it = exact.find(search_seq);
        if (it != exact.end()) {
            state.index = it->second;
            state.mismatches = 0;
        } else {
            matcher_in_the_rye<Index, Updator>(search_seq, cache, trie, state, allowed_mismatches, max_mm);
        }
    }

private:
    std::unordered_map<std::string, int> exact;
    AnyMismatches trie;
    std::unordered_map<std::string, std::pair<int, int> > cache;
    int max_mm;
};

/**
 * @brief Search for known barcode sequences with segmented mismatches.
 *
 * This supports exact and mismatch-aware searches for known sequences.
 * Mismatches are restricted by segments along the sequence, see `SegmentedMismatches` for details. 
 * Instances of this class use caching to avoid redundant work when a mismatching sequence has been previously encountered.
 *
 * @tparam num_segments Number of segments to consider.
 */
template<size_t num_segments>
class SegmentedBarcodeSearch {
public:
    /**
     * Default constructor.
     */
    SegmentedBarcodeSearch() {}

    /**
     * @param barcode_pool Pool of barcode sequences.
     * @param segments Size of each segment.
     * All values should be positive and their sum should be equal to the barcode length.
     * @param max_mismatches Maximum number of mismatches in each segment.
     * All values should be non-negative.
     * @param reverse Whether to reverse-complement the barcode sequences.
     * @param duplicates Whether duplicated `sequences` in `barcode_pool` are supported, see `MismatchTrie`.
     */
    SegmentedBarcodeSearch(const BarcodePool& barcode_pool, std::array<int, num_segments> segments, std::array<int, num_segments> max_mismatches, bool reverse = false, bool duplicates = false) : 
        trie(segments), 
        max_mm(max_mismatches) 
    {
        if (barcode_pool.length != trie.get_length()) {
            throw std::runtime_error("variable sequences should have the same length as the sum of segment lengths");
        }
        fill_library(barcode_pool.pool, exact, trie, reverse, duplicates);
        return;
    }

public:
    /**
     * @brief State of the search.
     *
     * This contains both the results of the search for any given input sequence,
     * as well as cached mismatches to optimize searches for future inputs.
     */
    struct State {
        /**
         * Index of the known sequence that matches best to the input sequence in `search()` (i.e., fewest total mismatches).
         * If no match was found or if the best match is ambiguous, this will be set to -1.
         */
        int index = 0;

        /**
         * Total number of mismatches with the matching known sequence, summed across all segments.
         * This should only be used if `index != -1`.
         */
        int mismatches = 0;

        /**
         * Number of mismatches in each segment.
         * This should only be used if `index != -1`.
         */
        std::array<int, num_segments> per_segment;
        
        /**
         * @cond
         */
        State() : per_segment() {}

        std::unordered_map<std::string, typename SegmentedMismatches<num_segments>::Result> cache;
        /**
         * @endcond
         */
    };

    /**
     * Initialize the search state for thread-safe execution.
     *
     * @return A new `SeachState()`.
     */
    State initialize() const {
        return State();
    }

    /**
     * Incorporate the mismatch cache from `state` into the cache for this `SimpleBarcodeSearch` instance.
     * This allows regular consolidation of optimizations across threads.
     *
     * @param state A state object generated by `initialize()`.
     * Typically this has already been used in `search()`.
     *
     * @return The mismatch cache of `state` is combined with that of this instance.
     */
    void reduce(State& state) {
        cache.merge(state.cache);
        state.cache.clear();
    }

private:
    typedef typename SegmentedMismatches<num_segments>::Result SegmentedResult;

    struct Index {
        static int index(const SegmentedResult& val) {
            return val.index;
        }
    };

    struct Updator {
        static void update(State& state, const SegmentedResult& val) {
            state.index = val.index;
            state.mismatches = val.total;
            state.per_segment = val.per_segment;
            return;
        }
    };

public:
    /**
     * Search the known sequences in the barcode pool for an input sequence.
     * The number of allowed mismatches in each segment is equal to the maximum specified in the constructor.
     * 
     * @param search_seq The input sequence to use for searching.
     * This is expected to have the same length as the known sequences.
     * @param state A state object generated by `initialize()`.
     *
     * @return `state` is filled with the details of the best-matching barcode sequence, if any exists.
     */
    void search(const std::string& search_seq, State& state) const {
        search(search_seq, state, max_mm);
        return;
    }

    /**
     * Search the known sequences in the barcode pool for an input sequence,
     * with potentially more stringent mismatch requirements for each segment.
     * This can improve efficiency in situations where some mismatches have already been consumed by matching the template sequence.
     *
     * @param search_seq The input sequence to use for searching.
     * This is expected to have the same length as the known sequences.
     * @param state A state object generated by `initialize()`.
     * @param allowed_mismatches Allowed number of mismatches in each segment.
     * Each value should not be greater than the corresponding maximum specified in the constructor.
     *
     * @return `state` is filled with the details of the best-matching barcode sequence, if any exists.
     */
    void search(const std::string& search_seq, State& state, std::array<int, num_segments> allowed_mismatches) const {
        auto it = exact.find(search_seq);
        if (it != exact.end()) {
            state.index = it->second;
            state.mismatches = 0;
            std::fill_n(state.per_segment.begin(), num_segments, 0);
        } else {
            matcher_in_the_rye<Index, Updator>(search_seq, cache, trie, state, allowed_mismatches, max_mm);
        }
    }

private:
    std::unordered_map<std::string, int> exact;
    SegmentedMismatches<num_segments> trie;
    std::unordered_map<std::string, SegmentedResult> cache;
    std::array<int, num_segments> max_mm;
};

}

#endif

#ifndef KAORI_DUAL_BARCODES_HPP
#define KAORI_DUAL_BARCODES_HPP

#include "../ConstantTemplate.hpp"
#include "../VariableLibrary.hpp"
#include "../utils.hpp"

/**
 * @file DualBarcodes.hpp
 *
 * @brief Process dual barcodes.
 */

namespace kaori {

/**
 * @brief Handler for dual barcodes.
 *
 * One of the paired reads contains a barcode from one pool of options, while the other read contains a barcode from another pool.
 * Unlike `CombinatorialBarcodesPairedEnd`, the combinations are known in advance, typically corresponding to specific pairs of genes.
 * This handler will capture the frequencies of each barcode combination. 
 *
 * @tparam N Size of the bitset to use for each constant template.
 * The maximum size of the template is defined as `N / 4`, see `ConstantTemplate` for details.
 */
template<size_t N>
class DualBarcodes { 
public:
    /**
     * @param[in] con1 Template sequence for the first barcode.
     * This should contain one variable region.
     * @param n1 Length of the first barcode template.
     * @param rev1 Whether to search the reverse strand for the first barcode template.
     * @param var1 Set of known sequences for the variable region in the first barcode.
     * @param mm1 Maximum number of mismatches for the first barcode.
     * @param[in] con2 Template sequence for the second barcode.
     * This should contain one variable region.
     * @param n2 Length of the second barcode template.
     * @param rev2 Whether to search the reverse strand for the second barcode template.
     * @param var2 Set of known sequences for the variable region in the second barcode.
     * @param mm2 Maximum number of mismatches for the second barcode.
     * @param random Whether the reads are randomized with respect to the first/second barcode.
     * If `false`, the first read is searched for the first barcode only, and the second read is searched for the second barcode only.
     * If `true`, an additional search will be performed in the opposite orientation.
     *
     * `var2` and `var1` are assumed to have the same number of barcodes.
     * Corresponding values across `var1` and `var2` define a particular combination. 
     */
    DualBarcodes(
        const char* con1, size_t n1, bool rev1, const SequenceSet& var1, int mm1, 
        const char* con2, size_t n2, bool rev2, const SequenceSet& var2, int mm2,
        bool random = false
    ) :
        reverse1(rev1),
        reverse2(rev2),
        constant1(con1, n1, !reverse1, reverse1),
        constant2(con2, n2, !reverse2, reverse2),
        max_mismatches1(mm1),
        max_mismatches2(mm2),
        randomized(random)
    {
        auto num_options = var1.size();
        if (num_options != var2.size()) {
            throw std::runtime_error("each read should contain the same number of choices for the variable region");
        }
        counts.resize(num_options);

        size_t len1;
        {
            const auto& regions = constant1.variable_regions();
            if (regions.size() != 1) { 
                throw std::runtime_error("expected one variable region in the first constant template");
            }
            len1 = regions[0].second - regions[0].first;
            if (len1 != var1.length) {
                throw std::runtime_error("length of variable sequences (" + std::to_string(var1.length) + ") should be the same as the variable region (" + std::to_string(len1) + ")");
            }
        }

        size_t len2;
        {
            const auto& regions = constant2.variable_regions();
            if (regions.size() != 1) { 
                throw std::runtime_error("expected one variable region in the second constant template");
            }
            len2 = regions[0].second - regions[0].first;
            if (len2 != var2.length) {
                throw std::runtime_error("length of variable sequences (" + std::to_string(var2.length) + ") should be the same as the variable region (" + std::to_string(len2) + ")");
            }
        }

        // Constructing the combined strings.
        std::vector<std::string> combined;
        combined.reserve(num_options);

        for (size_t i = 0; i < num_options; ++i) {
            std::string current;

            auto ptr1 = var1[i];
            if (reverse1) {
                for (int j = 0; j < len1; ++j) {
                    current += reverse_complement(ptr1[len1 - j - 1]);
                }
            } else {
                current.insert(current.end(), ptr1, ptr1 + len1);
            }

            auto ptr2 = var2[i];
            if (reverse2) {
                for (int j = 0; j < len2; ++j) {
                    current += reverse_complement(ptr2[len2 - j - 1]);
                }
            } else {
                current.insert(current.end(), ptr2, ptr2 + len2);
            }

            combined.push_back(std::move(current));
        }

        // Constructing the combined varlib.
        SequenceSet combined_set(combined);
        varlib = SegmentedVariableLibrary(
            combined_set,
            std::array<int, 2>{ static_cast<int>(len1), static_cast<int>(len2) }, 
            std::array<int, 2>{ max_mismatches1, max_mismatches2 }
        );
    }

    /**
     * @param t Whether to search only for the first match across both reads.
     * If `false`, the handler will search for the best match (i.e., fewest mismatches) instead.
     *
     * @return A reference to this `DualBarcodes` instance.
     */
    DualBarcodes& set_first(bool t = true) {
        use_first = t;
        return *this;
    }

public:
    /**
     *@cond
     */
    struct State {
        State(size_t n = 0) : counts(n) {}
        std::vector<int> counts;
        int total = 0;

        std::vector<std::pair<std::string, int> > buffer2;

        // Default constructors should be called in this case, so it should be fine.
        typename SegmentedVariableLibrary<2>::SearchState details;
    };

    State initialize() const {
        return State(counts.size());
    }

    void reduce(State& s) {
        varlib.reduce(s.details);
        for (size_t i = 0; i < counts.size(); ++i) {
            counts[i] += s.counts[i];
        }
        total += s.total;
    }

    constexpr static bool use_names = false;
    /**
     * @endcond
     */

private:
    static void emit_output(std::pair<std::string, int>& output, const char* start, const char* end, int mm) {
        output.first = std::string(start, end);
        output.second = mm;
        return;
    }

    static void emit_output(std::vector<std::pair<std::string, int> >& output, const char* start, const char* end, int mm) {
        output.emplace_back(std::string(start, end), mm);
        return;
    }

    template<class Store>
    static bool inner_process(
        bool reverse, 
        const ConstantTemplate<N>& constant, 
        int max_mismatches,
        const char* against,
        typename ConstantTemplate<N>::MatchDetails& deets,
        Store& output)
    {
        while (!deets.finished) {
            constant.next(deets);
            if (reverse) {
                if (deets.reverse_mismatches <= max_mismatches) {
                    const auto& reg = constant.template variable_regions<true>()[0];
                    auto start = against + deets.position;
                    emit_output(output, start + reg.first, start + reg.second, deets.reverse_mismatches);
                    return true;
                }
            } else {
                if (deets.forward_mismatches <= max_mismatches) {
                    const auto& reg = constant.variable_regions()[0];
                    auto start = against + deets.position;
                    emit_output(output, start + reg.first, start + reg.second, deets.forward_mismatches);
                    return true;
                }
            }
        }
        return false;
    }

    bool process_first(State& state, const std::pair<const char*, const char*>& against1, const std::pair<const char*, const char*>& against2) const {
        auto deets1 = constant1.initialize(against1.first, against1.second - against1.first);
        std::pair<std::string, int> match1;

        auto deets2 = constant2.initialize(against2.first, against2.second - against2.first);
        state.buffer2.clear();

        auto checker = [&](size_t idx2) -> bool {
            const auto& current2 = state.buffer2[idx2];
            auto combined = match1.first + current2.first;
            varlib.match(combined, state.details, std::array<int, 2>{ max_mismatches1 - match1.second, max_mismatches2 - current2.second });

            if (state.details.index != -1) {
                ++state.counts[state.details.index];
                return true;
            } else {
                return false;
            }
        };

        // Looping over all hits of the second for each hit of the first.
        while (inner_process(reverse1, constant1, max_mismatches1, against1.first, deets1, match1)) {
            if (deets2.finished) {
                for (size_t i = 0; i < state.buffer2.size(); ++i) {
                    if (checker(i)) {
                        return true;
                    }
                }
            } else {
                while (inner_process(reverse2, constant2, max_mismatches2, against2.first, deets2, state.buffer2)) {
                    if (checker(state.buffer2.size() - 1)) {
                        return true;
                    }
                }
                if (state.buffer2.empty()) {
                    break;
                }
            }
        }

        return false;
    }

    std::pair<int, int> process_best(State& state, const std::pair<const char*, const char*>& against1, const std::pair<const char*, const char*>& against2) const {
        auto deets1 = constant1.initialize(against1.first, against1.second - against1.first);
        std::pair<std::string, int> match1;

        auto deets2 = constant2.initialize(against2.first, against2.second - against2.first);
        state.buffer2.clear();

        int chosen = -1;
        int best_mismatches = max_mismatches1 + max_mismatches2 + 1;

        auto checker = [&](size_t idx2) -> void {
            const auto& current2 = state.buffer2[idx2];
            auto combined = match1.first + current2.first;
            varlib.match(combined, state.details, std::array<int, 2>{ max_mismatches1 - match1.second, max_mismatches2 - current2.second });

            int cur_mismatches = state.details.mismatches;
            if (cur_mismatches < best_mismatches) {
                chosen = state.details.index;
                best_mismatches = cur_mismatches;
            } else if (cur_mismatches == best_mismatches && chosen != state.details.index) { // ambiguous.
                chosen = -1;
            }
        };

        while (inner_process(reverse1, constant1, max_mismatches1, against1.first, deets1, match1)) {
            if (deets2.finished) {
                for (size_t i = 0; i < state.buffer2.size(); ++i) {
                    checker(i);
                }
            } else {
                while (inner_process(reverse2, constant2, max_mismatches2, against2.first, deets2, state.buffer2)) {
                    checker(state.buffer2.size() - 1);
                }
                if (state.buffer2.empty()) {
                    break;
                }
            }
        }

        return std::make_pair(chosen, best_mismatches);
    }

public:
    /**
     *@cond
     */
    bool process(State& state, const std::pair<const char*, const char*>& r1, const std::pair<const char*, const char*>& r2) const {
        bool found;

        if (use_first) {
            found = process_first(state, r1, r2);
            if (!found && randomized) {
                found = process_first(state, r2, r1);
            }

        } else {
            auto best = process_best(state, r1, r2);
            if (randomized) {
                auto best2 = process_best(state, r2, r1);
                if (best.first < 0 || best.second > best2.second) {
                    best = best2;
                } else if (best.second == best2.second && best.first != best2.first) {
                    best.first = -1; // ambiguous.
                }
            }

            found = best.first >= 0;
            if (found) {
                ++state.counts[best.first];
            }
        }

        ++state.total;
        return found;
    }
    /**
     *@endcond
     */

private:
    bool reverse1, reverse2;

    ConstantTemplate<N> constant1, constant2;
    SegmentedVariableLibrary<2> varlib;
    int max_mismatches1, max_mismatches2;

    bool randomized;
    bool use_first = true;

    std::vector<int> counts;
    int total = 0;

public:
    /**
     * @return Vector containing the frequency of each valid combination.
     * This has length equal to the number of valid combinations (i.e., the length of `var1` and `var2` in the constructor).
     */
    const std::vector<int>& get_counts() const {
        return counts;
    }

    /**
     * @return Total number of read pairs processed by the handler.
     */
    int get_total() const {
        return total;
    }
};

}

#endif

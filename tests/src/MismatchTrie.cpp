#include <gtest/gtest.h>
#include "kaori/MismatchTrie.hpp"
#include <string>
#include "utils.h"

TEST(AnyMismatches, Basic) {
    std::vector<std::string> things { "ACGT", "AAAA", "ACAA", "AGTT" };
    kaori::BarcodePool ptrs(things);
    kaori::AnyMismatches stuff(ptrs);

    {
        auto res = stuff.search("ACGT", 0);
        EXPECT_EQ(res.first, 0);
        EXPECT_EQ(res.second, 0);
    }

    {
        auto res = stuff.search("AAAT", 1);
        EXPECT_EQ(res.first, 1);
        EXPECT_EQ(res.second, 1);
    }

    {
        auto res = stuff.search("CCAG", 2);
        EXPECT_EQ(res.first, 2);
        EXPECT_EQ(res.second, 2);
    }

    {
        auto res = stuff.search("AGTT", 0);
        EXPECT_EQ(res.first, 3);
        EXPECT_EQ(res.second, 0);
    }
}

TEST(AnyMismatches, MoreMismatches) {
    std::vector<std::string> things { "ACGTACGTACGT", "TTTGGGCCCAAA" };
    kaori::BarcodePool ptrs(things);
    kaori::AnyMismatches stuff(ptrs);

    {
        auto res = stuff.search("ACGTACGTCCGT", 2);
        EXPECT_EQ(res.first, 0);
        EXPECT_EQ(res.second, 1);
    }

    {
        auto res = stuff.search("TCGTACGTCCGT", 2);
        EXPECT_EQ(res.first, 0);
        EXPECT_EQ(res.second, 2);
    }

    {
        auto res = stuff.search("TTTGGGGCCAAA", 2);
        EXPECT_EQ(res.first, 1);
        EXPECT_EQ(res.second, 1);
    }

    {
        auto res = stuff.search("TTGGGGGCCAAA", 2);
        EXPECT_EQ(res.first, 1);
        EXPECT_EQ(res.second, 2);
    }
}

TEST(AnyMismatches, MismatchesWithNs) {
    std::vector<std::string> things { "ACGTACGTACGT", "TTTGGGCCCAAA" };
    kaori::BarcodePool ptrs(things);
    kaori::AnyMismatches stuff(ptrs);

    {
        auto res = stuff.search("ACGTACGTACGN", 0);
        EXPECT_EQ(res.first, -1);

        auto res2 = stuff.search("ACGTACGTACGN", 1);
        EXPECT_EQ(res2.first, 0);
        EXPECT_EQ(res2.second, 1);
    }

    {
        auto res = stuff.search("TTNGGGNCCAAA", 1);
        EXPECT_EQ(res.first, -1);

        auto res2 = stuff.search("TTNGGGNCCAAA", 2);
        EXPECT_EQ(res2.first, 1);
        EXPECT_EQ(res2.second, 2);
    }
}

TEST(AnyMismatches, CappedMismatch) {
    // Force an early return.
    std::vector<std::string> things { "ACGT", "AAAA", "ACAA", "AGTT" };
    kaori::BarcodePool ptrs(things);
    kaori::AnyMismatches stuff(ptrs);

    {
        auto res = stuff.search("AAAT", 0);
        EXPECT_EQ(res.first, -1);
        EXPECT_EQ(res.second, 1);
    }

    {
        auto res = stuff.search("CCAG", 1);
        EXPECT_EQ(res.first, -1);
        EXPECT_EQ(res.second, 2);
    }
}

TEST(AnyMismatches, Ambiguous) {
    std::vector<std::string> things { "AAAAGAAAA", "AAAACAAAA", "AAAAAAAAG", "AAAAAAAAC" };
    kaori::BarcodePool ptrs(things);
    kaori::AnyMismatches stuff(ptrs);

    {
        // Positive control first.
        auto res = stuff.search("AAAACAAAA", 1);
        EXPECT_EQ(res.first, 1);
        EXPECT_EQ(res.second, 0);
    }

    {
        auto res = stuff.search("AAAATAAAA", 1);
        EXPECT_EQ(res.first, -1);
        EXPECT_EQ(res.second, 1);
    }

    {
        // Handles ambiguity at the end of the sequence.
        auto res = stuff.search("AAAAAAAAT", 1);
        EXPECT_EQ(res.first, -1);
        EXPECT_EQ(res.second, 1);
    }
}

TEST(AnyMismatches, Duplicates) {
    std::vector<std::string> things { "ACGT", "ACGT", "AGTT", "AGTT" };
    kaori::BarcodePool ptrs(things);

    EXPECT_ANY_THROW({
        try {
            kaori::AnyMismatches stuff(ptrs);
        } catch (std::exception& e) {
            EXPECT_TRUE(std::string(e.what()).find("duplicate") != std::string::npos);
            throw e;
        }
    });

    // Gets the first occurrence.
    kaori::AnyMismatches stuff(ptrs, true);
    auto res = stuff.search("ACGT", 0);
    EXPECT_EQ(res.first, 0);

    auto res2= stuff.search("AGTT", 0);
    EXPECT_EQ(res2.first, 2);
}

TEST(SegmentedMismatches, Segmented) {
    std::vector<std::string> things { "AAAAAA", "CCCCCC", "GGGGGG", "TTTTTT" };
    kaori::BarcodePool ptrs(things);
    kaori::SegmentedMismatches<2> stuff(ptrs, {4, 2});

    {
        auto res = stuff.search("AAAAAAA", { 0, 0 });
        EXPECT_EQ(res.index, 0);
        EXPECT_EQ(res.total, 0);
        EXPECT_EQ(res.per_segment[0], 0);
        EXPECT_EQ(res.per_segment[1], 0);
    }

    {
        auto res = stuff.search("TTTTTT", { 0, 0 });
        EXPECT_EQ(res.index, 3);
        EXPECT_EQ(res.total, 0);
        EXPECT_EQ(res.per_segment[0], 0);
        EXPECT_EQ(res.per_segment[1], 0);
    }

    // Fails on one mismatch.
    {
        auto res = stuff.search("CCCCCTC", { 0, 0 });
        EXPECT_EQ(res.index, -1);
    }
}

TEST(SegmentedMismatches, Mismatches) {
    std::vector<std::string> things { "AAAAAA", "CCCCCC", "GGGGGG", "TTTTTT" };
    kaori::BarcodePool ptrs(things);
    kaori::SegmentedMismatches<2> stuff(ptrs, {4, 2});

    // Handles one mismatch.
    {
        auto res = stuff.search("CCCCTC", { 0, 1 });
        EXPECT_EQ(res.index, 1);
        EXPECT_EQ(res.total, 1);
        EXPECT_EQ(res.per_segment[0], 0);
        EXPECT_EQ(res.per_segment[1], 1);
    }

    // But not in the wrong place.
    {
        auto res = stuff.search("CCCCTC", { 1, 0 });
        EXPECT_EQ(res.index, -1);
    }

    // Testing the handling of mismatches at the end.
    {
        auto res = stuff.search("TTTTTA", { 0, 1 });
        EXPECT_EQ(res.index, 3);
        EXPECT_EQ(res.total, 1);
        EXPECT_EQ(res.per_segment[0], 0);
        EXPECT_EQ(res.per_segment[1], 1);
    }

    // Mismatches in both.
    {
        auto res = stuff.search("GGTGGC", { 1, 1 });
        EXPECT_EQ(res.index, 2);
        EXPECT_EQ(res.total, 2);
        EXPECT_EQ(res.per_segment[0], 1);
        EXPECT_EQ(res.per_segment[1], 1);
    }

    // More mismatches.
    {
        auto res = stuff.search("GGTGTC", { 1, 1 });
        EXPECT_EQ(res.index, -1);
    }

    {
        auto res = stuff.search("GGTGTC", { 2, 2 });
        EXPECT_EQ(res.index, 2);
        EXPECT_EQ(res.total, 3);
        EXPECT_EQ(res.per_segment[0], 1);
        EXPECT_EQ(res.per_segment[1], 2);
    }
}

TEST(SegmentedMismatches, MismatchesWithNs) {
    std::vector<std::string> things { "AAAAAA", "CCCCCC", "GGGGGG", "TTTTTT" };
    kaori::BarcodePool ptrs(things);
    kaori::SegmentedMismatches<2> stuff(ptrs, {4, 2});

    {
        auto res = stuff.search("CCCCNC", { 0, 1 });
        EXPECT_EQ(res.index, 1);
        EXPECT_EQ(res.total, 1);
        EXPECT_EQ(res.per_segment[0], 0);
        EXPECT_EQ(res.per_segment[1], 1);
    }

    {
        auto res = stuff.search("GNGGGN", { 1, 2 });
        EXPECT_EQ(res.index, 2);
        EXPECT_EQ(res.total, 2);
        EXPECT_EQ(res.per_segment[0], 1);
        EXPECT_EQ(res.per_segment[1], 1);
    }

    // Not in the wrong place, though.
    {
        auto res = stuff.search("CCCCNC", { 1, 0 });
        EXPECT_EQ(res.index, -1);
    }
}


TEST(SegmentedMismatches, Ambiguity) {
    {
        std::vector<std::string> things { "AAAAAA", "CCCCCC", "GGGGGG", "TTTTTT" };
        kaori::BarcodePool ptrs(things);
        kaori::SegmentedMismatches<2> stuff(ptrs, {4, 2});

        // Handles ambiguity properly.
        {
            auto res = stuff.search("GGTGTT", { 2, 2 });
            EXPECT_EQ(res.index, -1);
        }

        {
            auto res = stuff.search("TTGGTG", { 2, 2 });
            EXPECT_EQ(res.index, -1);
        }

        {
            auto res = stuff.search("GGGTTT", { 2, 2 });
            EXPECT_EQ(res.index, -1);
        }
    }

    // Handles ambiguity properly at the end
    {
        std::vector<std::string> things { "AAAAAA", "AAAAAT" };
        kaori::BarcodePool ptrs(things);
        kaori::SegmentedMismatches<2> stuff(ptrs, {4, 2});

        auto res = stuff.search("AAAAAC", { 0, 1 });
        EXPECT_EQ(res.index, -1);
    }
}

#include <gtest/gtest.h>
#include "dsa/DLL.h"
#include <string>

namespace {
    class DLLTest : public ::testing::Test {
    protected:
        dsa::DLL<std::string> list;

        void SetUp() override {
            list.addLast("Standard");
            list.addLast("Tekken 8");
            list.addLast("LoL");
        }
    };
}

TEST_F(DLLTest, SizeVerification) {
    EXPECT_EQ(list.size(), 3);
    EXPECT_EQ(list.getCurrent(), "Standard");
}

TEST_F(DLLTest, ForwardCycleWrapAround) {
    EXPECT_EQ(list.cycleNext(), "Tekken 8");
    EXPECT_EQ(list.cycleNext(), "LoL");
    EXPECT_EQ(list.cycleNext(), "Standard"); // Wrap around to Head
    EXPECT_EQ(list.cycleNext(), "Tekken 8");
}

TEST_F(DLLTest, BackwardCycleWrapAround) {
    EXPECT_EQ(list.cyclePrev(), "LoL");      // Wrap around to Tail
    EXPECT_EQ(list.cyclePrev(), "Tekken 8");
    EXPECT_EQ(list.cyclePrev(), "Standard");
}

TEST_F(DLLTest, CursorPointerSafetyOnDeletion) {
    ASSERT_TRUE(list.setCursorTo("Tekken 8"));
    ASSERT_EQ(list.getCurrent(), "Tekken 8");

    // Delete active node
    list.remove("Tekken 8");

    EXPECT_EQ(list.size(), 2);
    // Safe read: cursor safely moved to "LoL", never dangling!
    EXPECT_NO_THROW({
        std::string current = list.getCurrent();
        EXPECT_TRUE(current == "Standard" || current == "LoL");
    });
}

TEST_F(DLLTest, SetCursorToTarget) {
    EXPECT_TRUE(list.setCursorTo("LoL"));
    EXPECT_EQ(list.getCurrent(), "LoL");

    EXPECT_EQ(list.cycleNext(), "Standard");

    EXPECT_FALSE(list.setCursorTo("NonExistentProfile"));
    EXPECT_EQ(list.getCurrent(), "Standard");
}

TEST_F(DLLTest, MoveConstructorStealsResources) {
    dsa::DLL<std::string> movedList(std::move(list));

    EXPECT_EQ(movedList.size(), 3);
    EXPECT_EQ(movedList.getCurrent(), "Standard");

    // Source list was cleanly hollowed out!
    EXPECT_EQ(list.size(), 0);
    EXPECT_THROW(list.getCurrent(), std::runtime_error);
}
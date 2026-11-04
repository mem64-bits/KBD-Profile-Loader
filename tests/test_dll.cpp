#include <gtest/gtest.h>
#include "dsa/DLL.h"
#include <string>
#include <vector>

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
// ---------------------------------------------------------------------------
// moveElement: the element at `from` ends up at `to`
// ---------------------------------------------------------------------------

namespace {

    std::vector<std::string> contents(const dsa::DLL<std::string>& list) {
        std::vector<std::string> out;
        list.forEach([&](const std::string& s) { out.push_back(s); });
        return out;
    }

    // Walks the list backwards from the tail, which only works if every m_previous link is right.
    // (With the cursor on the head, cyclePrev() wraps to the tail first.)
    std::vector<std::string> backwards(dsa::DLL<std::string>& list) {
        std::vector<std::string> out;
        list.setCursorTo(contents(list).front());
        for (std::size_t i = 0; i < list.size(); ++i) out.insert(out.begin(), list.cyclePrev());
        return out;
    }

    class DLLMoveTest : public ::testing::Test {
    protected:
        void SetUp() override {
            for (const char* name : {"A", "B", "C", "D", "E"}) list.addLast(name);
        }
        dsa::DLL<std::string> list;
    };

} // namespace

TEST_F(DLLMoveTest, MovesForwardIntoTheMiddle) {
    EXPECT_TRUE(list.moveElement(1, 3));
    EXPECT_EQ(contents(list), (std::vector<std::string>{"A", "C", "D", "B", "E"}));
}

TEST_F(DLLMoveTest, MovesBackwardIntoTheMiddle) {
    EXPECT_TRUE(list.moveElement(3, 1));
    EXPECT_EQ(contents(list), (std::vector<std::string>{"A", "D", "B", "C", "E"}));
}

TEST_F(DLLMoveTest, MovesHeadToTailAndBack) {
    EXPECT_TRUE(list.moveElement(0, 4));
    EXPECT_EQ(contents(list), (std::vector<std::string>{"B", "C", "D", "E", "A"}));
    EXPECT_TRUE(list.moveElement(4, 0));
    EXPECT_EQ(contents(list), (std::vector<std::string>{"A", "B", "C", "D", "E"}));
}

TEST_F(DLLMoveTest, MovesTailIntoTheMiddle) {
    EXPECT_TRUE(list.moveElement(4, 2));
    EXPECT_EQ(contents(list), (std::vector<std::string>{"A", "B", "E", "C", "D"}));
}

TEST_F(DLLMoveTest, BackLinksStayConsistent) {
    ASSERT_TRUE(list.moveElement(0, 2));
    ASSERT_TRUE(list.moveElement(4, 1));
    const auto forward = contents(list);
    EXPECT_EQ(backwards(list), forward);
}

TEST_F(DLLMoveTest, InvalidMovesChangeNothing) {
    EXPECT_FALSE(list.moveElement(2, 2));
    EXPECT_FALSE(list.moveElement(5, 0));
    EXPECT_FALSE(list.moveElement(0, 5));
    EXPECT_EQ(contents(list), (std::vector<std::string>{"A", "B", "C", "D", "E"}));
    EXPECT_EQ(list.size(), 5u);
}

TEST_F(DLLMoveTest, TheCursorStaysOnTheSameElement) {
    ASSERT_TRUE(list.setCursorTo("C"));
    ASSERT_TRUE(list.moveElement(2, 0));
    EXPECT_EQ(list.getCurrent(), "C");
    EXPECT_EQ(list.cycleNext(), "A");
}

#include <gtest/gtest.h>

#include <irreden/common/components/component_undo_history.hpp>
#include <irreden/common/undo.hpp>
#include <irreden/ir_entity.hpp>

#include <chrono>
#include <stdexcept>
#include <string>

namespace {

using History = IRComponents::C_UndoHistory;

History::Record
record(std::string label, int &value, int undoValue, int redoValue, std::string key = {}) {
    return {
        std::move(label),
        [&value, undoValue]() { value = undoValue; },
        [&value, redoValue]() { value = redoValue; },
        std::move(key),
    };
}

TEST(UndoHistoryTest, PushUndoRedoAndBranchTruncation) {
    History history;
    int value = 1;
    const IRTime::TimePoint t{};

    EXPECT_TRUE(history.push(record("first", value, 0, 1), t));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(history.canUndo());
    EXPECT_FALSE(history.canRedo());
    EXPECT_EQ(history.labels().undo_, "first");

    EXPECT_TRUE(history.undo());
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(history.canRedo());
    EXPECT_EQ(history.labels().redo_, "first");
    EXPECT_TRUE(history.redo());
    EXPECT_EQ(value, 1);

    EXPECT_TRUE(history.undo());
    value = 2;
    EXPECT_TRUE(history.push(record("replacement", value, 0, 2), t));
    EXPECT_FALSE(history.canRedo());
    EXPECT_EQ(history.labels().undo_, "replacement");
}

TEST(UndoHistoryTest, CoalescingKeepsFirstUndoAndLatestRedoAtExactBoundary) {
    History history;
    int value = 2;
    const IRTime::TimePoint t{};

    EXPECT_TRUE(history.push(record("drag one", value, 0, 1, "slider"), t));
    EXPECT_TRUE(
        history.push(record("drag two", value, 1, 2, "slider"), t + std::chrono::milliseconds(500))
    );
    EXPECT_EQ(history.labels().undo_, "drag two");
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(history.canUndo());
    EXPECT_TRUE(history.redo());
    EXPECT_EQ(value, 2);
}

TEST(UndoHistoryTest, EmptyKeysAndExpiredWindowsDoNotCoalesce) {
    History history;
    int value = 2;
    const IRTime::TimePoint t{};

    history.push(record("one", value, 0, 1), t);
    history.push(record("two", value, 1, 2), t);
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(value, 0);

    value = 2;
    history.clear();
    history.push(record("one", value, 0, 1, "slider"), t);
    history.push(record("two", value, 1, 2, "slider"), t + std::chrono::milliseconds(501));
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(value, 0);
}

TEST(UndoHistoryTest, DepthCapEvictsOldestRecord) {
    History history;
    history.depth_ = 256;
    int value = 256;
    const IRTime::TimePoint t{};
    for (int i = 0; i < 257; ++i) {
        history.push(record(std::to_string(i), value, i, i + 1), t);
    }
    int undoCount = 0;
    while (history.undo()) {
        ++undoCount;
    }
    EXPECT_EQ(undoCount, 256);
    EXPECT_EQ(value, 1);
}

TEST(UndoHistoryTest, ContextsAreIsolatedAndClearRestoresDefault) {
    History history;
    int value = 1;
    const IRTime::TimePoint t{};
    const History::ContextId other = history.createContext();

    history.push(record("default", value, 0, 1), t);
    ASSERT_TRUE(history.selectContext(other));
    EXPECT_FALSE(history.canUndo());
    value = 4;
    history.push(record("other", value, 3, 4), t);
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(value, 3);
    ASSERT_TRUE(history.selectContext(History::kDefaultContext));
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(value, 0);

    EXPECT_TRUE(history.clear());
    EXPECT_FALSE(history.canUndo());
    EXPECT_FALSE(history.selectContext(other));
}

TEST(UndoHistoryTest, MutationDuringDispatchIsRejected) {
    History history;
    int value = 1;
    bool nestedPushAccepted = true;
    const IRTime::TimePoint t{};
    History::Record nested = record("nested", value, 0, 2);
    history.push(
        {"outer", [&]() { nestedPushAccepted = history.push(std::move(nested), t); }, []() {}, {}},
        t
    );

    EXPECT_TRUE(history.undo());
    EXPECT_FALSE(nestedPushAccepted);
    EXPECT_FALSE(history.canUndo());
}

TEST(UndoHistoryTest, FacadeRequiresRegistrationBeforeUse) {
    IREntity::EntityManager entityManager;
    int value = 1;
    EXPECT_THROW(IRPrefab::Undo::push(record("unregistered", value, 0, 1)), std::runtime_error);
    EXPECT_EQ(IREntity::singletonOrNull<History>(), nullptr);
}

} // namespace

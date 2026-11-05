#include <gtest/gtest.h>
#include <irreden/window/close_guard.hpp>
#include <memory>

namespace {

TEST(WindowCloseGuard, UnguardedWindowAcceptsOnlyRequestedClose) {
    IRWindow::CloseGuard guard;
    EXPECT_FALSE(guard.accepts(false));
    EXPECT_TRUE(guard.accepts(true));
}

TEST(WindowCloseGuard, IdleFramesDoNotInvokeConfirmation) {
    IRWindow::CloseGuard guard;
    int calls = 0;
    guard.set([&calls]() {
        ++calls;
        return false;
    });
    EXPECT_FALSE(guard.accepts(false));
    EXPECT_EQ(calls, 0);
    EXPECT_FALSE(guard.accepts(true));
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(guard.accepts(false));
    EXPECT_EQ(calls, 1);
}

TEST(WindowCloseGuard, ConfirmationReevaluatesCurrentDocumentState) {
    IRWindow::CloseGuard guard;
    bool dirty = true;
    guard.set([&dirty]() { return !dirty; });
    EXPECT_FALSE(guard.accepts(true));
    dirty = false;
    EXPECT_TRUE(guard.accepts(true));
    dirty = true;
    EXPECT_FALSE(guard.accepts(true));
}

TEST(WindowCloseGuard, RemovingGuardReleasesCaptureAndRestoresClose) {
    IRWindow::CloseGuard guard;
    auto state = std::make_shared<int>(0);
    const std::weak_ptr<int> lifetime = state;
    guard.set([state]() { return false; });
    state.reset();
    EXPECT_FALSE(lifetime.expired());
    guard.set({});
    EXPECT_TRUE(lifetime.expired());
    EXPECT_TRUE(guard.accepts(true));
}

} // namespace

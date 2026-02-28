#include <gtest/gtest.h>
#include "approval/ApprovalQueue.hpp"

TEST(ApprovalQueueTest, AutoAllApprovesEverything) {
    ApprovalQueue queue(ApprovalQueue::Mode::AutoAll);

    MixAction action;
    action.type    = ActionType::SetFader;
    action.channel = 1;
    action.urgency = MixAction::Urgency::Normal;

    EXPECT_TRUE(queue.submit(action));
    EXPECT_EQ(queue.pendingCount(), 0);
}

TEST(ApprovalQueueTest, DenyAllRejectsEverything) {
    ApprovalQueue queue(ApprovalQueue::Mode::DenyAll);

    MixAction action;
    action.type    = ActionType::SetFader;
    action.channel = 1;
    action.urgency = MixAction::Urgency::Normal;

    EXPECT_FALSE(queue.submit(action));
}

TEST(ApprovalQueueTest, AutoUrgentApprovesImmediateActions) {
    ApprovalQueue queue(ApprovalQueue::Mode::AutoUrgent);

    MixAction urgent;
    urgent.type    = ActionType::SetFader;
    urgent.channel = 1;
    urgent.urgency = MixAction::Urgency::Immediate;

    EXPECT_TRUE(queue.submit(urgent));
    EXPECT_EQ(queue.pendingCount(), 0);
}

TEST(ApprovalQueueTest, AutoUrgentQueuesFastActions) {
    ApprovalQueue queue(ApprovalQueue::Mode::AutoUrgent);

    MixAction fast;
    fast.type    = ActionType::SetFader;
    fast.channel = 1;
    fast.urgency = MixAction::Urgency::Fast;

    EXPECT_TRUE(queue.submit(fast));  // Fast is auto-approved too
}

TEST(ApprovalQueueTest, AutoUrgentQueuesNormalActions) {
    ApprovalQueue queue(ApprovalQueue::Mode::AutoUrgent);

    MixAction normal;
    normal.type    = ActionType::SetFader;
    normal.channel = 1;
    normal.urgency = MixAction::Urgency::Normal;

    EXPECT_FALSE(queue.submit(normal));  // queued, not auto-approved
    EXPECT_EQ(queue.pendingCount(), 1);
}

TEST(ApprovalQueueTest, ManualApproveWorks) {
    ApprovalQueue queue(ApprovalQueue::Mode::ApproveAll);

    MixAction action;
    action.type    = ActionType::SetFader;
    action.channel = 1;
    action.value   = 0.5f;
    action.urgency = MixAction::Urgency::Normal;

    queue.submit(action);
    EXPECT_EQ(queue.pendingCount(), 1);

    queue.approve(0);
    EXPECT_EQ(queue.pendingCount(), 0);

    MixAction out;
    EXPECT_TRUE(queue.popApproved(out, 100));
    EXPECT_EQ(out.channel, 1);
    EXPECT_FLOAT_EQ(out.value, 0.5f);
}

TEST(ApprovalQueueTest, ManualRejectWorks) {
    ApprovalQueue queue(ApprovalQueue::Mode::ApproveAll);

    MixAction action;
    action.type    = ActionType::SetFader;
    action.channel = 1;
    action.urgency = MixAction::Urgency::Normal;

    queue.submit(action);
    queue.reject(0);
    EXPECT_EQ(queue.pendingCount(), 0);

    MixAction out;
    EXPECT_FALSE(queue.popApproved(out, 100));
}

TEST(ApprovalQueueTest, ApproveAllClears) {
    ApprovalQueue queue(ApprovalQueue::Mode::ApproveAll);

    for (int i = 0; i < 5; i++) {
        MixAction a;
        a.type = ActionType::SetFader;
        a.channel = i + 1;
        a.urgency = MixAction::Urgency::Normal;
        queue.submit(a);
    }
    EXPECT_EQ(queue.pendingCount(), 5);

    queue.approveAll();
    EXPECT_EQ(queue.pendingCount(), 0);

    // All 5 should be poppable
    for (int i = 0; i < 5; i++) {
        MixAction out;
        EXPECT_TRUE(queue.popApproved(out, 100));
        EXPECT_EQ(out.channel, i + 1);
    }
}

TEST(ApprovalQueueTest, ModeCanBeChanged) {
    ApprovalQueue queue(ApprovalQueue::Mode::AutoAll);
    EXPECT_EQ(queue.mode(), ApprovalQueue::Mode::AutoAll);

    queue.setMode(ApprovalQueue::Mode::DenyAll);
    EXPECT_EQ(queue.mode(), ApprovalQueue::Mode::DenyAll);
}

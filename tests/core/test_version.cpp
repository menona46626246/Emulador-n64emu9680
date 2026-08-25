#include <gtest/gtest.h>

#include "n64/common/version.hpp"
#include "n64/core/scheduler.hpp"

using namespace n64;

TEST(Version, ProjectName) {
    EXPECT_EQ(project_name(), "n64emu");
}

TEST(Version, ToStringNonEmpty) {
    const auto v = version();
    EXPECT_GE(v.major, 0);
    EXPECT_GE(v.minor, 0);
    EXPECT_FALSE(v.to_string().empty());
    EXPECT_NE(v.to_string().find('.'), std::string::npos);
}

TEST(Scheduler, ScheduleAndDispatch) {
    Scheduler sch;
    sch.reset();
    EXPECT_TRUE(sch.empty());

    const u64 t1 = sch.schedule_in(10, EventKind::VerticalInterrupt);
    const u64 t2 = sch.schedule_in(5, EventKind::AiDmaComplete);
    EXPECT_NE(t1, t2);
    EXPECT_EQ(sch.pending(), 2u);

    int fired_ai = 0;
    int fired_vi = 0;
    auto handler = [&](const ScheduledEvent& e) {
        if (e.kind == EventKind::AiDmaComplete) {
            ++fired_ai;
        }
        if (e.kind == EventKind::VerticalInterrupt) {
            ++fired_vi;
        }
    };

    sch.advance(4);
    EXPECT_EQ(sch.dispatch_due(handler), 0u);

    sch.advance(1); // now == 5
    EXPECT_EQ(sch.dispatch_due(handler), 1u);
    EXPECT_EQ(fired_ai, 1);
    EXPECT_EQ(fired_vi, 0);

    sch.advance(5); // now == 10
    EXPECT_EQ(sch.dispatch_due(handler), 1u);
    EXPECT_EQ(fired_vi, 1);
    EXPECT_TRUE(sch.empty());
}

TEST(Scheduler, Cancel) {
    Scheduler sch;
    const u64 t = sch.schedule_in(3, EventKind::User);
    EXPECT_TRUE(sch.cancel(t));
    EXPECT_FALSE(sch.cancel(t)); // already gone
    sch.advance(10);
    EXPECT_EQ(sch.dispatch_due([](const ScheduledEvent&) {}), 0u);
}

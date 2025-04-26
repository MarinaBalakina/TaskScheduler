#include <gtest/gtest.h>
#include "scheduler.h"

struct Adder {
    explicit Adder(float b = 0.f) : bias(b) {}
    float add(float x) const { return x + bias; }
    float bias;
};

static_assert(is_future_result<future_result<int>>::value);
static_assert(!is_future_result<int>::value);
static_assert(std::is_same_v<unwrap_future<future_result<double>>::type, double>);
static_assert(std::is_same_v<unwrap_future<long>::type, long>);

TEST(Scheduler, SingleConstantTask) {
    TTaskScheduler sch;
    int id = sch.add([] { return 42; });
    EXPECT_EQ(sch.getResult<int>(id), 42);
}

TEST(Scheduler, ChainOfTasks) {
    TTaskScheduler sch;
    auto idA = sch.add([] { return 3.0f; });
    auto idB = sch.add([](float x) { return x * x; },
                       sch.getFutureResult<float>(idA));
    auto idC = sch.add([](float y) { return y + 1.0f; },
                       sch.getFutureResult<float>(idB));
    EXPECT_FLOAT_EQ(sch.getResult<float>(idC), 10.0f);
}

TEST(Scheduler, TwoArgTask) {
    TTaskScheduler sch;
    auto idX = sch.add([] { return 2.0; });
    auto idY = sch.add([] { return 5.0; });
    auto idSum = sch.add([](double a, double b) { return a + b; },
                         sch.getFutureResult<double>(idX),
                         sch.getFutureResult<double>(idY));
    EXPECT_DOUBLE_EQ(sch.getResult<double>(idSum), 7.0);
}

TEST(Scheduler, MemberFunctionTaskConst) {
    TTaskScheduler sch;
    Adder add5{5.0f};
    auto idVal = sch.add([] { return 3.5f; });
    auto idRes = sch.add(&Adder::add, add5,
                         sch.getFutureResult<float>(idVal));
    EXPECT_FLOAT_EQ(sch.getResult<float>(idRes), 8.5f);
}

TEST(Scheduler, ExecuteAllRunsEverything) {
    TTaskScheduler sch;
    auto id1 = sch.add([] { return 4; });
    auto id2 = sch.add([](int v) { return v * 10; },
                       sch.getFutureResult<int>(id1));
    sch.executeAll();
    EXPECT_EQ(sch.getResult<int>(id1), 4);
    EXPECT_EQ(sch.getResult<int>(id2), 40);
}

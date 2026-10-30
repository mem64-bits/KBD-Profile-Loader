#include <gtest/gtest.h>

#include "ui/AppModel.h"

#include <thread>
#include <vector>

namespace {

    class CountingObserver : public ui::IAppModelObserver {
    public:
        void onAppStateChanged(const ui::AppSnapshot& snapshot) override {
            ++calls;
            last = snapshot;
        }

        int             calls = 0;
        ui::AppSnapshot last;
    };

}

TEST(AppModelTest, StartsWithUnknownDisconnectedState) {
    ui::AppModel model;
    const auto state = model.snapshot();

    EXPECT_FALSE(state.device.connected);
    EXPECT_EQ(state.activeSlot, -1);
    EXPECT_FALSE(state.busy);
    EXPECT_EQ(state.status.severity, ui::StatusSeverity::None);
    for (const auto& slot : state.slots) EXPECT_FALSE(slot.hasProfile);
}

TEST(AppModelTest, UpdateChangesTheSnapshotAndNotifiesObservers) {
    ui::AppModel model;
    CountingObserver observer;
    model.addObserver(&observer);
    const auto before = model.revision();

    model.update([](ui::AppSnapshot& state) { state.activeSlot = 2; });

    EXPECT_EQ(model.snapshot().activeSlot, 2);
    EXPECT_EQ(observer.calls, 1);
    EXPECT_EQ(observer.last.activeSlot, 2);
    EXPECT_EQ(model.revision(), before + 1);
    model.removeObserver(&observer);
}

TEST(AppModelTest, NoOpUpdatesAreSilent) {
    ui::AppModel model;
    model.update([](ui::AppSnapshot& state) { state.activeSlot = 1; });
    CountingObserver observer;
    model.addObserver(&observer);
    const auto before = model.revision();

    model.update([](ui::AppSnapshot& state) { state.activeSlot = 1; }); // same value
    model.update([](ui::AppSnapshot&) {});                              // does nothing

    EXPECT_EQ(observer.calls, 0);
    EXPECT_EQ(model.revision(), before);
    model.removeObserver(&observer);
}

TEST(AppModelTest, RepeatedStatusTextStillCountsAsAChange) {
    ui::AppModel model;
    CountingObserver observer;
    model.addObserver(&observer);

    model.setStatus(ui::StatusSeverity::Info, "Working");
    const auto first = model.snapshot().status.serial;
    model.setStatus(ui::StatusSeverity::Info, "Working");

    EXPECT_EQ(observer.calls, 2);
    EXPECT_GT(model.snapshot().status.serial, first);
    EXPECT_EQ(model.snapshot().status.text, "Working");
    model.removeObserver(&observer);
}

TEST(AppModelTest, RemovedObserverIsNotNotified) {
    ui::AppModel model;
    CountingObserver observer;
    model.addObserver(&observer);
    model.removeObserver(&observer);

    model.update([](ui::AppSnapshot& state) { state.busy = true; });

    EXPECT_EQ(observer.calls, 0);
}

TEST(AppModelTest, ObserverMayReadTheModelFromItsCallback) {
    struct Reader : ui::IAppModelObserver {
        explicit Reader(ui::AppModel& m) : model(m) {}
        void onAppStateChanged(const ui::AppSnapshot&) override { seen = model.snapshot().activeSlot; }
        ui::AppModel& model;
        int seen = -99;
    };
    ui::AppModel model;
    Reader reader(model);
    model.addObserver(&reader);

    model.update([](ui::AppSnapshot& state) { state.activeSlot = 1; });

    EXPECT_EQ(reader.seen, 1);
    model.removeObserver(&reader);
}

TEST(AppModelTest, ConcurrentWritersNeverLoseAnUpdate) {
    ui::AppModel model;
    constexpr int THREADS = 4;
    constexpr int UPDATES = 500;

    std::vector<std::thread> workers;
    for (int t = 0; t < THREADS; ++t) {
        workers.emplace_back([&model] {
            for (int i = 0; i < UPDATES; ++i) {
                model.update([](ui::AppSnapshot& state) { ++state.activeSlot; });
                (void)model.snapshot();
            }
        });
    }
    for (auto& worker : workers) worker.join();

    EXPECT_EQ(model.snapshot().activeSlot, -1 + THREADS * UPDATES);
    EXPECT_EQ(model.revision(), static_cast<std::uint64_t>(THREADS * UPDATES));
}

TEST(AppStateTest, LabelsAreOneBased) {
    EXPECT_EQ(ui::slotLabel(0), "Slot 1");
    EXPECT_EQ(ui::slotLabel(2), "Slot 3");
    EXPECT_STREQ(ui::transportName(kbd::TransportMode::Wired), "Wired USB-C");
    EXPECT_STREQ(ui::transportName(kbd::TransportMode::WirelessDongle), "2.4G dongle");
    EXPECT_STREQ(ui::transportName(kbd::TransportMode::Disconnected), "Disconnected");
}

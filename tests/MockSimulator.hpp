#pragma once

// A tiny deterministic stand-in for Geometry Dash, used by the test suite to
// exercise the search without the game. It models just enough of GD to be
// interesting: constant forward speed, gravity, jump buffering, pits that kill
// you when you are on the ground, and a level end.

#include "../src/core/Simulator.hpp"

#include <deque>
#include <utility>
#include <vector>

namespace pathfinder::test {

struct MockLevel {
    double length = 2000.0;
    double speed = 4.0;
    double gravity = -0.9;
    double jumpVelocity = 11.0;
    /// Ranges of X where touching the ground is fatal.
    std::vector<std::pair<double, double>> pits;
    /// Ranges of X where being airborne is fatal (ceilings / overhangs). These
    /// are what force the search to backtrack: jumping over a pit can be right
    /// locally and fatal a few windows later.
    std::vector<std::pair<double, double>> ceilings;
    /// Frames a jump press is buffered for, like GD's input buffer.
    uint32_t jumpBufferFrames = 4;
};

class MockSimulator : public Simulator {
public:
    explicit MockSimulator(MockLevel level = MockLevel{}) : m_level(level) {}

    struct State {
        double x = 0.0;
        double y = 0.0;
        double vy = 0.0;
        bool grounded = true;
        bool dead = false;
        bool finished = false;
        uint64_t frame = 0;
        int32_t jumpBuffer = -1;
    };

    bool ready() const override { return m_ready; }
    void setReady(bool ready) { m_ready = ready; }

    bool reset() override {
        releaseAll();
        m_suspended = false;
        m_state = State{};
        m_windowFrame = 0;
        m_plan.clear();
        m_planCursor = 0;
        m_held = false;
        m_resets++;
        return m_ready;
    }

    uint64_t frame() const override { return m_state.frame; }

    double progress() const override {
        if (m_level.length <= 0.0) return 0.0;
        double p = m_state.x / m_level.length;
        return p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
    }

    double posX() const override { return m_state.x; }
    double posY() const override { return m_state.y; }
    bool alive() const override { return !m_state.dead; }
    bool finished() const override { return m_state.finished; }
    bool onGround() const override { return m_state.grounded; }
    bool hasDynamicState() const override { return m_dynamic; }
    void setDynamic(bool value) { m_dynamic = value; }

    void beginWindow(Plan const& plan) override {
        m_plan = plan;
        m_planCursor = 0;
        m_windowFrame = 0;
    }

    uint32_t step(uint32_t maxFrames) override {
        uint32_t stepped = 0;
        for (uint32_t i = 0; i < maxFrames; i++) {
            if (m_state.dead || m_state.finished) break;

            // apply every input scheduled for this window frame
            while (m_planCursor < m_plan.size() && m_plan[m_planCursor].offset <= m_windowFrame) {
                PlanInput const& in = m_plan[m_planCursor++];
                if (in.button != Button::Jump) continue;
                if (in.holding) {
                    if (!m_held) m_state.jumpBuffer = static_cast<int32_t>(m_level.jumpBufferFrames);
                    m_held = true;
                } else {
                    m_held = false;
                }
            }

            advance();
            m_windowFrame++;
            m_state.frame++;
            stepped++;
        }
        return stepped;
    }

    bool suspendWindow() override {
        if (m_suspended) return true;
        m_suspend = Suspend{m_state, m_held, m_plan, m_planCursor, m_windowFrame};
        m_suspended = true;
        return true;
    }

    bool resumeWindow() override {
        if (!m_suspended) return false;
        m_state = m_suspend.state;
        m_held = m_suspend.held;
        m_plan = m_suspend.plan;
        m_planCursor = m_suspend.cursor;
        m_windowFrame = m_suspend.windowFrame;
        m_suspended = false;
        return true;
    }

    bool isSuspended() const override { return m_suspended; }

    Snapshot save(SnapshotKind kind) override {
        Entry entry;
        entry.state = m_state;
        entry.held = m_held;
        entry.kind = kind;
        m_snapshots.push_back(entry);
        m_saved++;
        return static_cast<Snapshot>(m_snapshots.size());
    }

    void restore(Snapshot handle) override {
        if (handle == NO_SNAPSHOT || handle > m_snapshots.size()) return;
        Entry const& entry = m_snapshots[handle - 1];
        m_state = entry.state;
        m_held = entry.held;
        m_windowFrame = 0;
        m_planCursor = 0;
        m_plan.clear();
        m_restored++;
    }

    void release(Snapshot handle) override {
        if (handle == NO_SNAPSHOT || handle > m_snapshots.size()) return;
        m_snapshots[handle - 1].released = true;
        m_released++;
    }

    void releaseAll() override {
        for (auto& entry : m_snapshots) entry.released = true;
        m_snapshots.clear();
    }

    // --- test instrumentation -------------------------------------------------
    uint64_t savedSnapshots() const { return m_saved; }
    uint64_t restoredSnapshots() const { return m_restored; }
    uint64_t releasedSnapshots() const { return m_released; }
    uint64_t resets() const { return m_resets; }
    /// Snapshots that were never released: a leak in the search bookkeeping.
    size_t liveSnapshots() const {
        size_t live = 0;
        for (auto const& entry : m_snapshots) {
            if (!entry.released) live++;
        }
        return live;
    }
    MockLevel const& level() const { return m_level; }

private:
    struct Entry {
        State state;
        bool held = false;
        SnapshotKind kind = SnapshotKind::Full;
        bool released = false;
    };

    struct Suspend {
        State state;
        bool held = false;
        Plan plan;
        size_t cursor = 0;
        uint32_t windowFrame = 0;
    };

    void advance() {
        if (m_state.jumpBuffer >= 0 && m_state.grounded) {
            m_state.vy = m_level.jumpVelocity;
            m_state.grounded = false;
            m_state.jumpBuffer = -1;
        } else if (m_state.jumpBuffer >= 0) {
            m_state.jumpBuffer--;
        }

        m_state.vy += m_level.gravity;
        m_state.y += m_state.vy;

        if (m_state.y <= 0.0) {
            m_state.y = 0.0;
            m_state.vy = 0.0;
            m_state.grounded = true;
        } else {
            m_state.grounded = false;
        }

        m_state.x += m_level.speed;

        if (m_state.grounded) {
            for (auto const& pit : m_level.pits) {
                if (m_state.x >= pit.first && m_state.x <= pit.second) {
                    m_state.dead = true;
                    break;
                }
            }
        } else {
            for (auto const& ceiling : m_level.ceilings) {
                if (m_state.x >= ceiling.first && m_state.x <= ceiling.second) {
                    m_state.dead = true;
                    break;
                }
            }
        }

        if (m_state.x >= m_level.length) m_state.finished = true;
    }

    MockLevel m_level;
    State m_state;
    bool m_held = false;
    bool m_ready = true;
    bool m_dynamic = false;
    uint32_t m_windowFrame = 0;
    Plan m_plan;
    size_t m_planCursor = 0;
    std::vector<Entry> m_snapshots;
    Suspend m_suspend;
    bool m_suspended = false;
    uint64_t m_saved = 0;
    uint64_t m_restored = 0;
    uint64_t m_released = 0;
    uint64_t m_resets = 0;
};

/// Replays a macro on a fresh mock level and reports whether it survives to the
/// end. This is the test suite's equivalent of "play the .slc in game".
struct ReplayOutcome {
    bool finished = false;
    bool died = false;
    double progress = 0.0;
    uint64_t frames = 0;
};

inline ReplayOutcome replayMacro(MockLevel const& level, Macro const& macro, uint64_t maxFrames = 2'000'000) {
    MockSimulator sim(level);
    sim.reset();

    // Only player events matter for the mock; index them by frame.
    std::vector<std::pair<uint64_t, MacroEvent>> timeline;
    for (auto const& e : macro.events) {
        if (isPlayerEvent(e.type)) timeline.push_back({e.frame, e});
    }
    size_t cursor = 0;

    Plan plan;
    sim.beginWindow(plan);

    ReplayOutcome out;
    bool held = false;
    for (uint64_t f = 0; f < maxFrames; f++) {
        Plan framePlan;
        while (cursor < timeline.size() && timeline[cursor].first == f) {
            PlanInput in;
            in.offset = 0;
            in.button = buttonOf(timeline[cursor].second.type);
            in.holding = timeline[cursor].second.holding;
            in.player2 = timeline[cursor].second.player2;
            framePlan.push_back(in);
            held = in.holding;
            cursor++;
        }
        // Keep the previous hold state across empty frames.
        if (framePlan.empty() && held) {
            // nothing to do: the mock keeps its internal held flag
        }
        sim.beginWindow(framePlan);
        if (sim.step(1) == 0) break;
        out.frames = f + 1;
        out.progress = sim.progress();
        if (!sim.alive()) {
            out.died = true;
            break;
        }
        if (sim.finished()) {
            out.finished = true;
            break;
        }
    }
    return out;
}

} // namespace pathfinder::test

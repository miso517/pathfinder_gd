#pragma once

// Simulator implementation that drives a real PlayLayer.
//
// The point of this class is that the search never has to guess how Geometry
// Dash physics work: every candidate is run through the game's own collision
// and movement code, at a fixed timestep, with the game's own checkpoint system
// used to rewind. Whatever the search concludes is therefore exactly what the
// game will do when the macro is played back.

#include <Geode/Bindings.hpp>

#include "../core/Simulator.hpp"
#include "SimSignals.hpp"

#include <string>
#include <vector>

using namespace geode::prelude;

namespace pathfinder {

class GDSimulator : public Simulator {
public:
    GDSimulator(PlayLayer* layer, double tps);
    ~GDSimulator() override;

    GDSimulator(GDSimulator const&) = delete;
    GDSimulator& operator=(GDSimulator const&) = delete;

    // --- Simulator -----------------------------------------------------------
    bool ready() const override;
    bool reset() override;

    uint64_t frame() const override { return m_frame; }
    double progress() const override;
    double posX() const override;
    double posY() const override;
    bool alive() const override;
    bool finished() const override;
    bool onGround() const override;
    bool dualMode() const override;
    bool platformer() const override;
    bool hasDynamicState() const override { return m_dynamicState; }

    void beginWindow(Plan const& plan) override;
    uint32_t step(uint32_t maxFrames) override;

    bool suspendWindow() override;
    bool resumeWindow() override;
    bool isSuspended() const override { return m_suspended; }

    Snapshot save(SnapshotKind kind) override;
    void restore(Snapshot handle) override;
    void release(Snapshot handle) override;
    void releaseAll() override;

    std::string describe() const override;

    // --- simulation control --------------------------------------------------
    /// Captures the state the level is in right now as the "start of the level"
    /// snapshot that `reset()` rewinds to. Only call this when the level really
    /// is at its beginning.
    bool captureRoot();

    /// True when death / completion effects should be swallowed because they
    /// belong to a simulation rather than to the player.
    bool suppressEffects() const;

    /// Called from the PlayerObject hook.
    void notifyDeath(PlayerObject* player);
    /// Called from the PlayLayer hook.
    void notifyFinish();

    double tps() const { return m_tps; }
    void setTps(double tps);
    float frameTime() const { return m_frameTime; }

    uint64_t stepsRun() const { return m_stepsRun; }
    uint64_t restoresRun() const { return m_restoresRun; }
    uint64_t checkpointsHeld() const;

    /// Player X at the start of the level, used for the progress heuristic.
    double startX() const { return m_startX; }
    double endX() const { return m_endX; }

private:
    struct Entry {
        CheckpointObject* checkpoint = nullptr;
        SnapshotKind kind = SnapshotKind::Full;
        uint64_t frame = 0;
        bool inUse = false;
    };

    Snapshot store(CheckpointObject* checkpoint, SnapshotKind kind);
    Entry* find(Snapshot handle);
    void applyRestore(Entry& entry);
    void measureLevelBounds();

    PlayLayer* m_layer = nullptr;
    double m_tps = 240.0;
    float m_frameTime = 1.f / 240.f;

    uint64_t m_frame = 0;
    uint32_t m_windowFrame = 0;
    Plan m_plan;
    size_t m_planCursor = 0;

    bool m_dead = false;
    bool m_levelFinished = false;
    bool m_dynamicState = false;

    CheckpointObject* m_root = nullptr;
    uint64_t m_rootFrame = 0;

    double m_startX = 0.0;
    double m_endX = 1.0;

    uint64_t m_stepsRun = 0;
    uint64_t m_restoresRun = 0;

    // suspended window (see Simulator::suspendWindow)
    bool m_suspended = false;
    CheckpointObject* m_suspendCheckpoint = nullptr;
    uint64_t m_suspendFrame = 0;
    uint32_t m_suspendWindowFrame = 0;
    Plan m_suspendPlan;
    size_t m_suspendCursor = 0;
    bool m_suspendDead = false;
    bool m_suspendFinished = false;

    void clearSuspend();

    std::vector<Entry> m_entries;
    std::vector<Snapshot> m_freeList;
};

} // namespace pathfinder

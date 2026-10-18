#pragma once

// Abstract "steppable level" used by the search.
//
// The real implementation (src/sim/GDSimulator) drives Geometry Dash itself so
// that the physics the search reasons about are exactly the physics the game
// will use when the macro is played back. A mock implementation lives in the
// test suite so the search logic can be exercised without the game.

#include "Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pathfinder {

/// A single button transition scheduled inside an evaluation window.
struct PlanInput {
    /// Frame offset from the start of the window.
    uint32_t offset = 0;
    Button button = Button::Jump;
    /// `true` = press, `false` = release.
    bool holding = true;
    bool player2 = false;
};

/// The inputs to apply while stepping through one evaluation window.
using Plan = std::vector<PlanInput>;

/// Opaque handle to a saved engine state. 0 means "no snapshot".
using Snapshot = uint32_t;
inline constexpr Snapshot NO_SNAPSHOT = 0;

enum class SnapshotKind {
    /// Whole world: players, dynamic/active objects, triggers.
    Full,
    /// Players only. Much cheaper to restore, but only valid while the level
    /// has no state that the player can change (breakables, toggles, ...).
    Player,
};

class Simulator {
public:
    virtual ~Simulator() = default;

    /// Whether the simulator can be used at all (level loaded, supported mode).
    virtual bool ready() const = 0;

    /// Restarts the level from the beginning and clears every snapshot.
    virtual bool reset() = 0;

    /// Absolute frame counter since the last `reset()` / `restore()`.
    virtual uint64_t frame() const = 0;

    /// Level completion in 0..1. Used as the main search objective.
    virtual double progress() const = 0;

    virtual double posX() const = 0;
    virtual double posY() const = 0;

    virtual bool alive() const = 0;
    virtual bool finished() const = 0;

    /// Extra tie breakers. Optional: the default implementations are neutral.
    virtual bool onGround() const { return false; }
    virtual bool dualMode() const { return false; }
    virtual bool platformer() const { return false; }
    /// True when the level contains state that player actions can change, which
    /// forces the search to use full snapshots between candidates.
    virtual bool hasDynamicState() const { return true; }

    /// Starts a new evaluation window: the plan cursor and the window frame
    /// counter are reset, so `offset` in `plan` is relative to now.
    virtual void beginWindow(Plan const& plan) = 0;

    /// Advances at most `maxFrames` frames, applying the current plan. Returns
    /// the number of frames actually stepped; stepping stops early on death or
    /// level completion.
    virtual uint32_t step(uint32_t maxFrames) = 0;

    /// Saves the middle of an evaluation window so the simulator can be rewound
    /// for display purposes and then picked up exactly where it left off.
    /// Returns false when suspending is not possible, in which case the caller
    /// has to restart the window from scratch.
    virtual bool suspendWindow() { return false; }
    /// Resumes a window suspended with `suspendWindow()`.
    virtual bool resumeWindow() { return false; }
    /// True while a window is suspended.
    virtual bool isSuspended() const { return false; }

    virtual Snapshot save(SnapshotKind kind) = 0;
    virtual void restore(Snapshot handle) = 0;
    virtual void release(Snapshot handle) = 0;
    virtual void releaseAll() = 0;

    /// Short human readable description of the current state, for the HUD.
    virtual std::string describe() const { return std::string(); }
};

} // namespace pathfinder

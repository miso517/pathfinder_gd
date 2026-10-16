#pragma once

// Candidate input plan generation for the rolling horizon search.
//
// For every segment of `horizon` frames the search tries a small, deterministic
// set of button patterns. The set is deliberately tiny: the game engine is the
// physics oracle, so each candidate costs real simulation time.

#include "Simulator.hpp"
#include "Types.hpp"

#include <cstdint>
#include <vector>

namespace pathfinder {

struct SearchParams {
    /// Ticks per second the search (and the exported macro) runs at.
    double tps = 240.0;
    /// Frames simulated per committed segment.
    uint32_t horizon = 16;
    /// Upper bound on candidates evaluated per segment.
    uint32_t maxPlansPerSegment = 24;
    /// Default jump hold length in frames.
    uint32_t holdFrames = 3;
    /// Also try Left/Right patterns (platformer levels).
    bool allowLeftRight = false;
    /// Emit inputs for player 2 as well (dual mode).
    bool dualMode = false;
    /// Progress of a plan that dies is multiplied by this before scoring, so a
    /// survivable plan always beats a deadly one that got further.
    double deathPenalty = 0.25;
    /// Tiny bonus for ending a window on the ground: breaks ties towards stable
    /// states instead of arbitrary ones.
    double groundBonus = 0.0005;
    /// Hard cap on simulated frames; the partial macro is kept when it is hit.
    uint64_t maxSimulatedFrames = 8'000'000;
    /// How many committed segments to keep snapshots for, i.e. how far the
    /// search can backtrack.
    uint32_t backtrackDepth = 4;
    /// Total number of backtrack operations before giving up.
    uint32_t maxBacktracks = 512;
    /// Prefer the cheap player-only restore when the level allows it.
    bool useCheapRestore = true;
    /// Kept for the exported macro's metadata.
    uint64_t seed = 0;
};

/// What is being held when a window starts, carried over from the previous one.
struct PlanContext {
    bool holdJump = false;
    bool holdLeft = false;
    bool holdRight = false;
};

/// Builds the candidate list for one window. The first entry is always the
/// "keep doing nothing new" plan so that coasting is always an option.
std::vector<Plan> generatePlans(SearchParams const& params, uint32_t horizon, PlanContext const& context);

/// Appends the window's presses/releases to `events`, shifted by `baseFrame`.
/// A press whose release falls outside the window is left open on purpose; the
/// caller tracks that in the PlanContext.
void planToEvents(Plan const& plan, uint64_t baseFrame, bool player2Mirror, std::vector<MacroEvent>& events);

/// Updates `context` with the buttons still held at the end of `plan`.
void planCarryOver(Plan const& plan, uint32_t horizon, PlanContext& context);

} // namespace pathfinder

#pragma once

// Rolling horizon (model predictive) search over the real game engine.
//
// The search never commits an input it has not simulated. For every window of
// `horizon` frames it
//
//   1. generates a small set of candidate button patterns,
//   2. restores the saved state and simulates each candidate,
//   3. scores them by level progress, punishing every candidate that dies,
//   4. commits the best surviving candidate and snapshots the new state.
//
// When no candidate survives, it backtracks to an earlier committed segment and
// tries that segment's next best candidate, which is what lets it escape traps
// where the fatal mistake happens a few jumps before the death.
//
// `tick()` is interruptible: the caller (the game's update loop) decides how many
// frames may be simulated per call, so the game never freezes for long.

#include "Plans.hpp"
#include "Simulator.hpp"
#include "Types.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace pathfinder {

enum class SearchStatus {
    NotStarted,
    Running,
    Finished, ///< The end of the level was reached.
    Failed,   ///< No survivable continuation exists (or the budget ran out).
    Cancelled, ///< The user stopped the search; a partial macro may exist.
};

/// Outcome of simulating one candidate plan.
struct ScoredPlan {
    double score = 0.0;
    double progress = 0.0;
    bool alive = false;
    bool finished = false;
    uint32_t frames = 0;
    Plan plan;
};

struct SearchStats {
    uint64_t simulatedFrames = 0;
    uint64_t segmentsCommitted = 0;
    uint64_t candidatesEvaluated = 0;
    uint64_t deathsObserved = 0;
    uint64_t backtracks = 0;
    double bestProgress = 0.0;
    /// Progress that is actually backed by committed (safe) input.
    double committedProgress = 0.0;
    uint64_t bestFrame = 0;
    /// True when the macro does not reach the end of the level.
    bool partial = true;
    std::string stopReason;
};

class Searcher {
public:
    Searcher(Simulator& sim, SearchParams params = SearchParams{});
    ~Searcher();

    Searcher(Searcher const&) = delete;
    Searcher& operator=(Searcher const&) = delete;

    /// Resets the level, takes the root snapshot and prepares the first window.
    bool start(std::string& error);

    /// Simulates at most `frameBudget` frames. Call repeatedly (e.g. once per
    /// game frame) until the returned status is no longer `Running`.
    SearchStatus tick(uint64_t frameBudget);

    /// Stops the search, keeping whatever was found so far.
    void cancel();

    /// Stops the search and releases every snapshot.
    void abort();

    /// Rewinds the simulator to the last committed state and throws away any
    /// partially simulated candidate. Called between ticks so that what the
    /// player sees on screen is the frontier of the search instead of whatever
    /// half finished candidate the budget ran out in the middle of.
    void showCommitted();

    SearchStatus status() const { return m_status; }
    SearchStats const& stats() const { return m_stats; }
    SearchParams const& params() const { return m_params; }
    SearchParams& params() { return m_params; }

    /// The macro built so far. Always sorted and playable up to `committedFrame`.
    Macro const& macro() const { return m_macro; }
    Macro& macro() { return m_macro; }

    uint64_t committedFrame() const { return m_baseFrame; }
    double committedProgress() const { return m_stats.committedProgress; }
    std::string statusText() const;

private:
    enum class Phase {
        Idle,
        Evaluate, ///< simulating the candidates of the current window
        Commit,   ///< replaying the winning candidate for real
        Backtrack,
        Done,
    };

    /// A committed segment: the state it started from plus the ranking that was
    /// used to pick its input. Kept so the search can undo it.
    struct Segment {
        Snapshot full = NO_SNAPSHOT;
        Snapshot player = NO_SNAPSHOT;
        uint64_t baseFrame = 0;
        size_t eventsBefore = 0;
        PlanContext context;
        std::vector<ScoredPlan> ranked;
        /// Index into `ranked` of the first candidate that has not been tried.
        size_t nextTry = 0;
    };

    void beginWindow();
    void evaluate(uint64_t& budget);
    void recordCandidate();
    void finishEvaluation();
    void startCommit(ScoredPlan const& best);
    void commit(uint64_t& budget);
    void finalizeCommit();
    void backtrack();

    void resumeIfSuspended();
    Snapshot cheapHandle() const;
    void releaseBase();
    /// Hands every snapshot back to the simulator. Snapshots are only needed
    /// while the search can still backtrack, so they are released as soon as it
    /// ends, whichever way it ends.
    void releaseHistory();
    void fail(std::string reason);
    double scoreOf(double progress, bool alive, bool finished, bool grounded) const;

    Simulator& m_sim;
    SearchParams m_params;
    SearchStats m_stats;
    SearchStatus m_status = SearchStatus::NotStarted;
    Phase m_phase = Phase::Idle;

    Macro m_macro;
    PlanContext m_context;

    // state the current window starts from
    Snapshot m_baseFull = NO_SNAPSHOT;
    Snapshot m_basePlayer = NO_SNAPSHOT;
    uint64_t m_baseFrame = 0;
    size_t m_baseEventCount = 0;

    // evaluation of the current window
    std::vector<Plan> m_candidates;
    std::vector<ScoredPlan> m_ranked;
    size_t m_candidateIndex = 0;
    uint32_t m_candidateFrames = 0;
    bool m_candidateStarted = false;

    // commit of the winning candidate
    Plan m_commitPlan;
    uint32_t m_commitTarget = 0;
    uint32_t m_commitFrames = 0;
    bool m_commitStarted = false;
    bool m_commitFinishedLevel = false;

    std::deque<Segment> m_segments;
    bool m_retrying = false;
    /// Set when `showCommitted()` had to rewind in the middle of a window; the
    /// next `tick()` picks the window back up.
    bool m_suspended = false;
};

} // namespace pathfinder

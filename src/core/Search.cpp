#include "Search.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pathfinder {

Searcher::Searcher(Simulator& sim, SearchParams params) : m_sim(sim), m_params(params) {
    if (m_params.horizon == 0) m_params.horizon = 1;
    if (m_params.maxPlansPerSegment == 0) m_params.maxPlansPerSegment = 1;
    if (m_params.backtrackDepth == 0) m_params.backtrackDepth = 1;
    if (!(m_params.tps > 0.0)) m_params.tps = 240.0;
}

Searcher::~Searcher() { abort(); }

void Searcher::releaseBase() {
    if (m_baseFull != NO_SNAPSHOT) {
        m_sim.release(m_baseFull);
        m_baseFull = NO_SNAPSHOT;
    }
    if (m_basePlayer != NO_SNAPSHOT) {
        m_sim.release(m_basePlayer);
        m_basePlayer = NO_SNAPSHOT;
    }
}

void Searcher::releaseHistory() {
    releaseBase();
    for (auto& segment : m_segments) {
        m_sim.release(segment.full);
        if (segment.player != NO_SNAPSHOT) m_sim.release(segment.player);
    }
    m_segments.clear();
    m_retrying = false;
}

void Searcher::fail(std::string reason) {
    m_stats.stopReason = std::move(reason);
    m_stats.partial = true;
    m_macro.frameCount = m_baseFrame;
    m_macro.normalize();
    m_status = SearchStatus::Failed;
    m_phase = Phase::Idle;
    releaseHistory();
}

void Searcher::cancel() {
    if (m_status != SearchStatus::Running) return;
    m_stats.stopReason = "cancelled";
    abort();
    m_status = SearchStatus::Cancelled;
}

void Searcher::abort() {
    m_suspended = false;
    releaseHistory();
    m_sim.releaseAll();
    m_phase = Phase::Idle;
    m_macro.frameCount = m_baseFrame;
    m_macro.normalize();
    if (m_status == SearchStatus::Running) m_status = SearchStatus::Cancelled;
}

void Searcher::showCommitted() {
    if (m_status != SearchStatus::Running) return;
    if (m_baseFull == NO_SNAPSHOT) return;

    bool const midWindow = (m_phase == Phase::Evaluate && m_candidateStarted && m_candidateFrames > 0) ||
                           (m_phase == Phase::Commit && m_commitStarted && m_commitFrames > 0);

    if (midWindow) {
        if (m_sim.suspendWindow()) {
            m_suspended = true;
        } else {
            // No way to pick this window back up, so it has to be restarted from
            // scratch on the next tick. Correct, just wasted work.
            m_candidateStarted = false;
            m_candidateFrames = 0;
            m_commitStarted = false;
            m_commitFrames = 0;
        }
    }

    m_sim.restore(m_baseFull);
}

void Searcher::resumeIfSuspended() {
    if (!m_suspended) return;
    m_suspended = false;
    if (m_sim.resumeWindow()) return;

    m_candidateStarted = false;
    m_candidateFrames = 0;
    m_commitStarted = false;
    m_commitFrames = 0;
}

bool Searcher::start(std::string& error) {
    if (!m_sim.ready()) {
        error = "the level is not ready to be simulated";
        return false;
    }

    m_sim.releaseAll();
    if (!m_sim.reset()) {
        error = "the level could not be reset for searching";
        return false;
    }

    m_macro = Macro{};
    m_macro.tps = m_params.tps;
    m_macro.seed = m_params.seed;
    m_macro.source = "search";
    m_context = PlanContext{};
    m_stats = SearchStats{};
    m_segments.clear();
    m_retrying = false;
    m_baseFrame = 0;
    m_baseEventCount = 0;
    m_commitStarted = false;

    m_baseFull = m_sim.save(SnapshotKind::Full);
    if (m_baseFull == NO_SNAPSHOT) {
        error = "could not snapshot the level state";
        return false;
    }
    m_basePlayer = m_params.useCheapRestore ? m_sim.save(SnapshotKind::Player) : NO_SNAPSHOT;

    m_status = SearchStatus::Running;
    beginWindow();
    return m_status == SearchStatus::Running;
}

Snapshot Searcher::cheapHandle() const {
    // Restoring only the players is a big win, but it silently ignores state the
    // player itself changes (breakable blocks, toggles, collected items), so it
    // is only used for levels that have none of that.
    if (m_basePlayer != NO_SNAPSHOT && !m_sim.hasDynamicState()) return m_basePlayer;
    return m_baseFull;
}

double Searcher::scoreOf(double progress, bool alive, bool finished, bool grounded) const {
    if (finished) return 1e9 + progress;
    if (!alive) return progress * m_params.deathPenalty;
    return progress + (grounded ? m_params.groundBonus : 0.0);
}

void Searcher::beginWindow() {
    m_candidates = generatePlans(m_params, m_params.horizon, m_context);
    m_ranked.clear();
    m_candidateIndex = 0;
    m_candidateFrames = 0;
    m_candidateStarted = false;
    m_commitStarted = false;

    if (m_candidates.empty()) {
        fail("no candidate inputs could be generated");
        return;
    }
    m_phase = Phase::Evaluate;
}

SearchStatus Searcher::tick(uint64_t frameBudget) {
    if (m_status != SearchStatus::Running) return m_status;

    resumeIfSuspended();

    uint64_t budget = frameBudget;
    uint32_t guard = 0;

    while (m_status == SearchStatus::Running) {
        bool const needsBudget = (m_phase == Phase::Evaluate || m_phase == Phase::Commit);
        if (needsBudget && budget == 0) break;

        switch (m_phase) {
            case Phase::Evaluate: evaluate(budget); break;
            case Phase::Commit: commit(budget); break;
            case Phase::Backtrack: backtrack(); break;
            case Phase::Done:
                m_status = SearchStatus::Finished;
                releaseHistory();
                break;
            case Phase::Idle: fail("the search was not started"); break;
        }

        if (++guard > 100000) {
            fail("the search did not make progress");
            break;
        }
    }

    return m_status;
}

void Searcher::evaluate(uint64_t& budget) {
    if (m_candidateIndex >= m_candidates.size()) {
        finishEvaluation();
        return;
    }

    while (budget > 0 && m_status == SearchStatus::Running) {
        if (m_candidateIndex >= m_candidates.size()) {
            finishEvaluation();
            return;
        }

        if (!m_candidateStarted) {
            m_sim.restore(cheapHandle());
            m_sim.beginWindow(m_candidates[m_candidateIndex]);
            m_candidateStarted = true;
            m_candidateFrames = 0;
        }

        uint32_t const windowLeft =
            (m_params.horizon > m_candidateFrames) ? static_cast<uint32_t>(m_params.horizon - m_candidateFrames) : 0u;
        uint32_t const want = static_cast<uint32_t>(std::min<uint64_t>(budget, windowLeft));
        uint32_t const stepped = (want > 0) ? m_sim.step(want) : 0u;

        budget -= stepped;
        m_candidateFrames += stepped;
        m_stats.simulatedFrames += stepped;

        if (stepped == 0 || !m_sim.alive() || m_sim.finished() || m_candidateFrames >= m_params.horizon) {
            recordCandidate();
        }

        if (m_stats.simulatedFrames >= m_params.maxSimulatedFrames) {
            fail("the simulated frame budget was exhausted");
            return;
        }
    }
}

void Searcher::recordCandidate() {
    ScoredPlan scored;
    scored.plan = m_candidates[m_candidateIndex];
    scored.frames = m_candidateFrames;
    scored.alive = m_sim.alive();
    scored.finished = m_sim.finished();
    scored.progress = m_sim.progress();
    scored.score = scoreOf(scored.progress, scored.alive, scored.finished, m_sim.onGround());

    m_ranked.push_back(scored);
    m_stats.candidatesEvaluated++;
    if (!scored.alive) m_stats.deathsObserved++;

    uint64_t const absoluteFrame = m_baseFrame + scored.frames;
    if (scored.progress > m_stats.bestProgress) m_stats.bestProgress = scored.progress;
    if (absoluteFrame > m_stats.bestFrame) m_stats.bestFrame = absoluteFrame;

    m_candidateIndex++;
    m_candidateStarted = false;
    m_candidateFrames = 0;
}

void Searcher::finishEvaluation() {
    if (m_ranked.empty()) {
        fail("no candidate could be simulated");
        return;
    }

    std::stable_sort(
        m_ranked.begin(), m_ranked.end(), [](ScoredPlan const& a, ScoredPlan const& b) { return a.score > b.score; }
    );

    if (!m_ranked.front().alive) {
        m_phase = Phase::Backtrack;
        return;
    }
    startCommit(m_ranked.front());
}

void Searcher::startCommit(ScoredPlan const& best) {
    if (best.frames == 0) {
        fail("the simulator could not advance the level");
        return;
    }
    m_commitPlan = best.plan;
    m_commitTarget = best.frames;
    m_commitFrames = 0;
    m_commitStarted = false;
    m_phase = Phase::Commit;
}

void Searcher::commit(uint64_t& budget) {
    if (!m_commitStarted) {
        // Candidate evaluation leaves the world dirty, so go back to the exact
        // state this window started from before committing anything.
        m_sim.restore(m_baseFull);
        m_sim.beginWindow(m_commitPlan);
        m_commitStarted = true;
        m_commitFrames = 0;
    }

    while (budget > 0 && m_commitFrames < m_commitTarget && m_sim.alive() && !m_sim.finished()) {
        uint32_t const want = static_cast<uint32_t>(std::min<uint64_t>(budget, m_commitTarget - m_commitFrames));
        uint32_t const stepped = m_sim.step(want);
        if (stepped == 0) break;
        budget -= stepped;
        m_commitFrames += stepped;
        m_stats.simulatedFrames += stepped;
    }

    if (m_commitFrames >= m_commitTarget || !m_sim.alive() || m_sim.finished()) finalizeCommit();
}

void Searcher::finalizeCommit() {
    if (!m_sim.alive() && !m_sim.finished()) {
        // The engine disagreed with the evaluation. Drop this candidate and try
        // the next best one instead of committing a macro that dies.
        if (!m_ranked.empty()) m_ranked.erase(m_ranked.begin());
        m_commitStarted = false;
        for (auto const& candidate : m_ranked) {
            if (candidate.alive) {
                startCommit(candidate);
                return;
            }
        }
        m_phase = Phase::Backtrack;
        return;
    }

    bool const ended = m_sim.finished();

    Plan emitted;
    emitted.reserve(m_commitPlan.size());
    for (auto const& input : m_commitPlan) {
        if (ended && input.offset >= m_commitFrames) continue;
        emitted.push_back(input);
    }
    planToEvents(emitted, m_baseFrame, m_params.dualMode, m_macro.events);
    planCarryOver(m_commitPlan, m_params.horizon, m_context);

    if (!m_retrying) {
        Segment segment;
        segment.full = m_baseFull;
        segment.player = m_basePlayer;
        segment.baseFrame = m_baseFrame;
        segment.eventsBefore = m_baseEventCount;
        segment.context = m_context;
        segment.ranked = m_ranked;
        segment.nextTry = 1; // ranked[0] was just committed
        m_segments.push_back(std::move(segment));

        // Ownership of the snapshots moved to the segment.
        m_baseFull = NO_SNAPSHOT;
        m_basePlayer = NO_SNAPSHOT;
    } else {
        m_retrying = false;
    }

    m_baseFrame += m_commitFrames;
    m_baseEventCount = m_macro.events.size();
    m_stats.segmentsCommitted++;
    m_stats.committedProgress = m_sim.progress();
    if (m_stats.committedProgress > m_stats.bestProgress) m_stats.bestProgress = m_stats.committedProgress;
    if (m_baseFrame > m_stats.bestFrame) m_stats.bestFrame = m_baseFrame;
    m_macro.frameCount = m_baseFrame;

    if (ended || m_sim.progress() >= 1.0) {
        m_stats.partial = false;
        m_stats.stopReason = "level completed";
        m_macro.normalize();
        m_phase = Phase::Done;
        return;
    }

    m_baseFull = m_sim.save(SnapshotKind::Full);
    if (m_baseFull == NO_SNAPSHOT) {
        fail("could not snapshot the level while searching");
        return;
    }
    m_basePlayer = m_params.useCheapRestore ? m_sim.save(SnapshotKind::Player) : NO_SNAPSHOT;

    while (m_segments.size() > m_params.backtrackDepth) {
        Segment& oldest = m_segments.front();
        m_sim.release(oldest.full);
        if (oldest.player != NO_SNAPSHOT) m_sim.release(oldest.player);
        m_segments.pop_front();
    }

    beginWindow();
}

void Searcher::backtrack() {
    for (;;) {
        if (!m_retrying) {
            if (m_segments.empty()) {
                fail("no survivable input exists for the first " + std::to_string(m_params.horizon) + " frames");
                return;
            }
            // Abandon the state we walked into; it has no future.
            releaseBase();
            m_retrying = true;
        } else {
            Segment& exhausted = m_segments.back();
            m_sim.release(exhausted.full);
            if (exhausted.player != NO_SNAPSHOT) m_sim.release(exhausted.player);
            m_segments.pop_back();
            m_baseFull = NO_SNAPSHOT;
            m_basePlayer = NO_SNAPSHOT;
            if (m_segments.empty()) {
                fail("backtracked past the start of the level without finding a survivable path");
                return;
            }
        }

        m_stats.backtracks++;
        if (m_stats.backtracks > m_params.maxBacktracks) {
            fail("the backtrack limit was reached");
            return;
        }

        Segment& segment = m_segments.back();
        if (segment.nextTry >= segment.ranked.size()) continue; // nothing left here, go one segment deeper

        m_baseFull = segment.full;
        m_basePlayer = segment.player;
        m_baseFrame = segment.baseFrame;
        m_baseEventCount = segment.eventsBefore;
        m_context = segment.context;
        if (m_macro.events.size() > m_baseEventCount) m_macro.events.resize(m_baseEventCount);

        m_candidates.clear();
        for (size_t i = segment.nextTry; i < segment.ranked.size(); i++) m_candidates.push_back(segment.ranked[i].plan);
        segment.nextTry = segment.ranked.size();

        m_ranked.clear();
        m_candidateIndex = 0;
        m_candidateFrames = 0;
        m_candidateStarted = false;
        m_commitStarted = false;

        m_sim.restore(m_baseFull);
        m_phase = Phase::Evaluate;
        return;
    }
}

std::string Searcher::statusText() const {
    auto percent = [](double p) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f%%", p * 100.0);
        return std::string(buf);
    };

    switch (m_status) {
        case SearchStatus::NotStarted: return "idle";
        case SearchStatus::Running:
            return "searching " + percent(m_stats.committedProgress) + " | " + std::to_string(m_stats.simulatedFrames) +
                   " frames | " + std::to_string(m_stats.backtracks) + " backtracks";
        case SearchStatus::Finished: return "completed " + percent(1.0);
        case SearchStatus::Cancelled: return "cancelled at " + percent(m_stats.bestProgress);
        case SearchStatus::Failed:
            return std::string("stopped at ") + percent(m_stats.bestProgress) +
                   (m_stats.stopReason.empty() ? "" : (" (" + m_stats.stopReason + ")"));
    }
    return "unknown";
}

} // namespace pathfinder

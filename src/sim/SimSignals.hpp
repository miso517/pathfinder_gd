#pragma once

// Process wide bridge between the Geode hooks and the running simulation.
//
// There is only ever one PlayLayer alive at a time, so a couple of file static
// values are enough. Keeping them here (instead of going through the session)
// means the hooks do not have to know anything about the search.

namespace pathfinder {

class GDSimulator;

namespace sim {

    /// True while the mod calls `GJBaseGameLayer::update` itself. The update hook
    /// uses this to avoid recursing into its own frame driving.
    bool& stepping();

    /// The simulator that should receive death / completion notifications, or
    /// null when nothing is being simulated.
    GDSimulator*& active();

    /// Sets `stepping()` for the lifetime of the object.
    struct StepGuard {
        StepGuard() : m_previous(stepping()) { stepping() = true; }
        ~StepGuard() { stepping() = m_previous; }

        StepGuard(StepGuard const&) = delete;
        StepGuard& operator=(StepGuard const&) = delete;

        bool m_previous;
    };

} // namespace sim

} // namespace pathfinder

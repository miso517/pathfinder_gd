#pragma once

// Pathfinder for Geometry Dash
// -----------------------------------------------------------------------------
// Core data types shared by the search engine, the macro codecs and the UI.
//
// IMPORTANT: everything in `src/core` and `src/format` is deliberately free of
// any Geometry Dash / Geode / cocos2d dependency so that it can be compiled and
// unit tested on its own (see `tests/`).
// -----------------------------------------------------------------------------

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace pathfinder {

/// Mirrors `slc::v3::Action::ActionType` one-to-one.
enum class EventType : uint8_t {
    Reserved = 0,
    // player buttons
    Jump = 1,
    Left = 2,
    Right = 3,
    // seed related
    Restart = 4,
    RestartFull = 5,
    Death = 6,
    // timing
    TPS = 7,
    // misc
    Bugpoint = 8,
};

/// The three buttons GD actually accepts through `GJBaseGameLayer::handleButton`.
enum class Button : uint8_t {
    Jump = 1,
    Left = 2,
    Right = 3,
};

inline constexpr bool isPlayerEvent(EventType type) {
    return type == EventType::Jump || type == EventType::Left || type == EventType::Right;
}

inline constexpr bool isSpecialEvent(EventType type) { return !isPlayerEvent(type) && type != EventType::Reserved; }

inline constexpr Button buttonOf(EventType type) { return static_cast<Button>(static_cast<uint8_t>(type)); }

inline constexpr EventType eventOf(Button button) { return static_cast<EventType>(static_cast<uint8_t>(button)); }

/// A single entry of a macro. Frame numbers are absolute, zero based and
/// counted in units of `1 / tps` seconds.
struct MacroEvent {
    uint64_t frame = 0;
    EventType type = EventType::Jump;
    /// `true` for a press, `false` for a release. Only meaningful for player events.
    bool holding = false;
    /// Dual mode: `true` routes the input to player 2.
    bool player2 = false;
    /// Payload for Restart / RestartFull / Death.
    uint64_t seed = 0;
    /// Payload for TPS.
    double tps = 240.0;

    static MacroEvent press(uint64_t frame, Button button, bool player2 = false) {
        MacroEvent e;
        e.frame = frame;
        e.type = eventOf(button);
        e.holding = true;
        e.player2 = player2;
        return e;
    }

    static MacroEvent release(uint64_t frame, Button button, bool player2 = false) {
        MacroEvent e;
        e.frame = frame;
        e.type = eventOf(button);
        e.holding = false;
        e.player2 = player2;
        return e;
    }
};

/// Ordering used both for sorting and for the on-disk layout: presses before
/// releases on the same frame, player 1 before player 2, jump before left/right.
inline constexpr bool eventOrderLess(MacroEvent const& a, MacroEvent const& b) {
    if (a.frame != b.frame) return a.frame < b.frame;
    if (a.holding != b.holding) return a.holding; // press first
    if (a.player2 != b.player2) return !a.player2;
    return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
}

/// A complete macro: an ordered event stream plus the timing information needed
/// to replay it, and some descriptive metadata that never hits the wire.
struct Macro {
    /// Ticks per second the frame numbers refer to. Silicate's default is 240.
    double tps = 240.0;
    /// Randomness seed recorded together with the macro (0 when unknown).
    uint64_t seed = 0;
    /// Sorted, normalized event stream.
    std::vector<MacroEvent> events;

    // --- descriptive metadata (not part of the .slc payload) -----------------
    std::string levelName;
    int levelID = 0;
    bool platformer = false;
    uint64_t frameCount = 0;
    std::string source = "search";
    std::string notes;

    void sort() { std::stable_sort(events.begin(), events.end(), eventOrderLess); }

    /// Sorts the stream and drops events that cannot be represented: reserved
    /// types, non-positive tps changes and duplicates.
    void normalize() {
        std::vector<MacroEvent> kept;
        kept.reserve(events.size());
        for (auto& e : events) {
            if (e.type == EventType::Reserved) continue;
            if (e.type == EventType::TPS && !(e.tps > 0.0)) continue;
            kept.push_back(e);
        }
        events.swap(kept);
        sort();
        // drop exact duplicates (same frame/button/player/hold)
        events.erase(
            std::unique(
                events.begin(), events.end(),
                [](MacroEvent const& a, MacroEvent const& b) {
                    return a.frame == b.frame && a.type == b.type && a.holding == b.holding && a.player2 == b.player2;
                }
            ),
            events.end()
        );
    }

    uint64_t lastFrame() const { return events.empty() ? 0 : events.back().frame; }

    size_t playerEventCount() const {
        size_t n = 0;
        for (auto const& e : events) {
            if (isPlayerEvent(e.type)) n++;
        }
        return n;
    }

    bool empty() const { return events.empty(); }
};

// --- integer helpers used by the codecs (same semantics as slc/util.hpp) -----

/// `floor(log2(n))`, clamped to 15 because the section headers only store 4 bits.
inline uint64_t exponentOfTwo(uint64_t n) {
    if (n == 0) return 0;
    uint64_t e = 0;
    while (n > 1) {
        n >>= 1;
        e++;
    }
    return e < 15 ? e : 15;
}

/// Largest power of two `<= n` (clamped to 1 << 15).
inline uint64_t largestPowerOfTwo(uint64_t n) { return n == 0 ? 0 : (1ull << exponentOfTwo(n)); }

} // namespace pathfinder

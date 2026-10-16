// Unit tests for the engine independent parts of the mod: the .slc codecs, the
// candidate plan generator and the rolling horizon search.
//
// These compile and run without Geometry Dash or Geode:
//     g++ -std=c++20 -O1 -o /tmp/pf-tests tests/main.cpp src/core/*.cpp src/format/*.cpp
// or simply `bash tests/run.sh`.

#include "../src/core/ByteIO.hpp"
#include "../src/core/Plans.hpp"
#include "../src/core/Search.hpp"
#include "../src/format/Dump.hpp"
#include "../src/format/Slc.hpp"
#include "MockSimulator.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace pathfinder;
using namespace pathfinder::test;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                                                  \
    do {                                                                                                             \
        g_checks++;                                                                                                  \
        if (!(cond)) {                                                                                               \
            g_failures++;                                                                                            \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                            \
        }                                                                                                            \
    } while (0)

#define SECTION(name) std::printf("* %s\n", name)

// ---------------------------------------------------------------------------

static bool sameEvent(MacroEvent const& a, MacroEvent const& b) {
    return a.frame == b.frame && a.type == b.type && a.holding == b.holding && a.player2 == b.player2 &&
           a.seed == b.seed && (a.type != EventType::TPS || a.tps == b.tps);
}

static void checkSameEvents(std::vector<MacroEvent> const& a, std::vector<MacroEvent> const& b, char const* what) {
    if (a.size() != b.size()) {
        g_failures++;
        g_checks++;
        std::printf("  FAIL %s: size %zu != %zu\n", what, a.size(), b.size());
        return;
    }
    for (size_t i = 0; i < a.size(); i++) {
        g_checks++;
        if (!sameEvent(a[i], b[i])) {
            g_failures++;
            std::printf(
                "  FAIL %s[%zu]: frame %llu/%llu type %d/%d hold %d/%d p2 %d/%d seed %llu/%llu tps %f/%f\n", what, i,
                (unsigned long long)a[i].frame, (unsigned long long)b[i].frame, (int)a[i].type, (int)b[i].type,
                (int)a[i].holding, (int)b[i].holding, (int)a[i].player2, (int)b[i].player2,
                (unsigned long long)a[i].seed, (unsigned long long)b[i].seed, a[i].tps, b[i].tps
            );
            return;
        }
    }
}

// ---------------------------------------------------------------------------

static void testHelpers() {
    SECTION("integer helpers");
    CHECK(exponentOfTwo(0) == 0);
    CHECK(exponentOfTwo(1) == 0);
    CHECK(exponentOfTwo(2) == 1);
    CHECK(exponentOfTwo(3) == 1);
    CHECK(exponentOfTwo(4) == 2);
    // section headers only store 4 bits, so counts clamp at 1 << 15 (like slc)
    CHECK(exponentOfTwo(65536) == 15);
    CHECK(exponentOfTwo(1ull << 40) == 15);
    CHECK(largestPowerOfTwo(0) == 0);
    CHECK(largestPowerOfTwo(5) == 4);
    CHECK(largestPowerOfTwo(8) == 8);
    CHECK(largestPowerOfTwo(100000) == 32768);

    CHECK(slc::deltaSizeClass(0, true) == 0);
    CHECK(slc::deltaSizeClass(15, true) == 0);
    CHECK(slc::deltaSizeClass(16, true) == 1);
    CHECK(slc::deltaSizeClass(4095, true) == 1);
    CHECK(slc::deltaSizeClass(4096, true) == 2);
    CHECK(slc::deltaSizeClass(268435455, true) == 2);
    CHECK(slc::deltaSizeClass(268435456, true) == 3);
    // specials reserve 8 bits instead of 4
    CHECK(slc::deltaSizeClass(255, false) == 0);
    CHECK(slc::deltaSizeClass(256, false) == 1);
    CHECK(slc::deltaSizeClass(65536, false) == 2);
}

static void testByteIO() {
    SECTION("byte io");
    ByteWriter w;
    w.u8(0xAB);
    w.u16(0x1234);
    w.u32(0xDEADBEEF);
    w.u64(0x0123456789ABCDEFull);
    w.f64(240.5);
    w.uintBytes(0x1122334455667788ull, 4);
    w.pad(3);

    std::vector<uint8_t> bytes = w.take();
    CHECK(bytes.size() == 1 + 2 + 4 + 8 + 8 + 4 + 3);

    ByteReader r(bytes);
    CHECK(*r.u8() == 0xAB);
    CHECK(*r.u16() == 0x1234);
    CHECK(*r.u32() == 0xDEADBEEF);
    CHECK(*r.u64() == 0x0123456789ABCDEFull);
    CHECK(*r.f64() == 240.5);
    CHECK(*r.uintBytes(4) == 0x55667788ull);
    CHECK(r.remaining() == 3);
    CHECK(r.skip(3));
    CHECK(r.eof());
    CHECK(!r.u8().has_value());
}

// ---------------------------------------------------------------------------

static Macro makeTrickyMacro() {
    Macro macro;
    macro.tps = 240.0;
    macro.seed = 0;
    macro.levelName = "Test Level";
    macro.events = {
        MacroEvent::press(0, Button::Jump),
        MacroEvent::release(15, Button::Jump),
        MacroEvent::press(16, Button::Jump),
        MacroEvent::release(4095, Button::Jump),
        MacroEvent::press(4096, Button::Jump),
        MacroEvent::release(100000, Button::Jump),
    };

    MacroEvent tps;
    tps.frame = 100050;
    tps.type = EventType::TPS;
    tps.tps = 300.0;
    macro.events.push_back(tps);

    macro.events.push_back(MacroEvent::press(100060, Button::Jump));
    macro.events.push_back(MacroEvent::release(100070, Button::Jump));

    MacroEvent restart;
    restart.frame = 100080;
    restart.type = EventType::Restart;
    restart.seed = 0xDEADBEEFull;
    macro.events.push_back(restart);

    // dual mode input for player 2 on the same frame as player 1
    macro.events.push_back(MacroEvent::press(300000000, Button::Jump, false));
    macro.events.push_back(MacroEvent::press(300000000, Button::Jump, true));
    macro.events.push_back(MacroEvent::release(300000001, Button::Jump, false));
    macro.events.push_back(MacroEvent::release(300000001, Button::Jump, true));

    macro.events.push_back(MacroEvent::press(300000002, Button::Left));
    macro.events.push_back(MacroEvent::release(300000003, Button::Left));
    macro.events.push_back(MacroEvent::press(300000004, Button::Right));
    macro.events.push_back(MacroEvent::release(300000005, Button::Right));

    macro.normalize();
    return macro;
}

static void testV3RoundTrip() {
    SECTION("slc v3 round trip");
    Macro macro = makeTrickyMacro();

    std::vector<uint8_t> bytes;
    std::string error;
    CHECK(slc::encode(macro, slc::Format::V3, bytes, error));
    CHECK(error.empty());
    CHECK(bytes.size() > 8 + 2 + 64 + 12);
    CHECK(bytes[0] == 'S' && bytes[7] == 'Y');
    CHECK(bytes.back() == slc::V3_FOOTER);

    Macro decoded;
    CHECK(slc::decode(bytes, decoded, error));
    CHECK(error.empty());
    checkSameEvents(macro.events, decoded.events, "v3");
    CHECK(decoded.tps == 300.0); // last TPS action wins, like Silicate
    CHECK(decoded.frameCount == macro.lastFrame());

    slc::Format format = slc::Format::V2;
    CHECK(slc::sniffFormat(bytes, format));
    CHECK(format == slc::Format::V3);
}

static void testV3Empty() {
    SECTION("slc v3 empty macro");
    Macro macro;
    std::vector<uint8_t> bytes;
    std::string error;
    CHECK(slc::encode(macro, slc::Format::V3, bytes, error));
    CHECK(error.empty());

    Macro decoded;
    CHECK(slc::decode(bytes, decoded, error));
    CHECK(error.empty());
    CHECK(decoded.events.empty());
}

static void testV2RoundTrip() {
    SECTION("slc v2 round trip");
    Macro macro = makeTrickyMacro();

    std::vector<uint8_t> bytes;
    std::string error;
    CHECK(slc::encode(macro, slc::Format::V2, bytes, error));
    CHECK(error.empty());
    CHECK(bytes[0] == 'S' && bytes[1] == 'I' && bytes[2] == 'L' && bytes[3] == 'L');
    CHECK(bytes.size() >= 3);
    CHECK(bytes[bytes.size() - 3] == 'E' && bytes[bytes.size() - 2] == 'O' && bytes[bytes.size() - 1] == 'M');

    Macro decoded;
    CHECK(slc::decode(bytes, decoded, error));
    CHECK(error.empty());

    // v2 cannot express Bugpoint and carries no seed for Restart/Death events,
    // so those two fields are dropped on the way through
    std::vector<MacroEvent> expected;
    for (auto const& e : macro.events) {
        if (e.type == EventType::Bugpoint) continue;
        MacroEvent adjusted = e;
        if (!isPlayerEvent(adjusted.type) && adjusted.type != EventType::TPS) adjusted.seed = 0;
        expected.push_back(adjusted);
    }
    checkSameEvents(expected, decoded.events, "v2");

    slc::Format format = slc::Format::V3;
    CHECK(slc::sniffFormat(bytes, format));
    CHECK(format == slc::Format::V2);
}

static void testV3HandmadeSections() {
    SECTION("slc v3 repeat sections and swift entries");

    // Build a container the way Silicate does: one Repeat section (2 entries
    // repeated 4 times) followed by one Input section holding a swift.
    ByteWriter payload;
    payload.u64(10); // 4 * 2 actions from the repeat + 2 from the swift

    // Repeat: id=1, deltaSize=0, countExp=1 (2 entries), repeatsExp=2 (4 times)
    uint16_t const repeatHeader = (1u << 14) | (0u << 12) | (1u << 8) | (2u << 3);
    payload.u16(repeatHeader);
    payload.u8(static_cast<uint8_t>((10u << 4) | (1u << 2) | 1u)); // press, delta 10
    payload.u8(static_cast<uint8_t>((5u << 4) | (1u << 2) | 0u));  // release, delta 5

    // Input: id=0, deltaSize=0, countExp=0 (1 entry), swift (button 0)
    uint16_t const inputHeader = (0u << 14) | (0u << 12) | (0u << 8);
    payload.u16(inputHeader);
    payload.u8(static_cast<uint8_t>((5u << 4) | (0u << 2) | 1u));

    ByteWriter w;
    w.raw(slc::V3_MAGIC, 8);
    w.u16(slc::V3_METADATA_SIZE);
    w.f64(240.0);
    w.u64(0);
    w.u32(2);
    w.u32(0);
    w.u32(0);
    w.pad(36);
    w.u32(static_cast<uint32_t>(slc::AtomId::Action));
    w.u64(payload.size());
    w.raw(payload.bytes().data(), payload.size());
    w.u8(slc::V3_FOOTER);

    std::vector<uint8_t> bytes = w.take();
    Macro decoded;
    std::string error;
    CHECK(slc::decode(bytes, decoded, error));
    CHECK(error.empty());

    CHECK(decoded.events.size() == 10);
    if (decoded.events.size() == 10) {
        // 4 repetitions of "press 10 frames later, release 5 frames after that"
        uint64_t expected[8][2] = {{10, 15}, {25, 30}, {40, 45}, {55, 60}};
        for (int rep = 0; rep < 4; rep++) {
            CHECK(decoded.events[rep * 2].frame == expected[rep][0]);
            CHECK(decoded.events[rep * 2].holding);
            CHECK(decoded.events[rep * 2 + 1].frame == expected[rep][1]);
            CHECK(!decoded.events[rep * 2 + 1].holding);
        }
        // the swift is a press and a release on the same frame
        CHECK(decoded.events[8].frame == 65);
        CHECK(decoded.events[8].holding);
        CHECK(decoded.events[9].frame == 65);
        CHECK(!decoded.events[9].holding);
    }

    // Re-encoding what we read must stay readable (no swift support needed).
    std::vector<uint8_t> reencoded;
    CHECK(slc::encode(decoded, slc::Format::V3, reencoded, error));
    Macro again;
    CHECK(slc::decode(reencoded, again, error));
    checkSameEvents(decoded.events, again.events, "v3 re-encode");
}

static void testDecodeErrors() {
    SECTION("slc decode rejects garbage");
    std::string error;
    Macro macro;

    std::vector<uint8_t> empty;
    CHECK(!slc::decode(empty, macro, error));
    CHECK(!error.empty());

    std::vector<uint8_t> garbage(64, 0x42);
    error.clear();
    CHECK(!slc::decode(garbage, macro, error));

    // truncated but valid magic
    std::vector<uint8_t> bytes;
    CHECK(slc::encode(makeTrickyMacro(), slc::Format::V3, bytes, error));
    std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + bytes.size() / 2);
    error.clear();
    CHECK(!slc::decode(truncated, macro, error));
    CHECK(!error.empty());

    // a lying atom size must not spin forever
    ByteWriter w;
    w.raw(slc::V3_MAGIC, 8);
    w.u16(64);
    w.f64(240.0);
    w.u64(0);
    w.u32(2);
    w.u32(0);
    w.u32(0);
    w.pad(36);
    w.u32(1);
    w.u64(0); // zero sized atom
    w.u8(slc::V3_FOOTER);
    std::vector<uint8_t> zeroAtom = w.take();
    error.clear();
    CHECK(!slc::decode(zeroAtom, macro, error));
    CHECK(!error.empty());
}

static void testFileIO() {
    SECTION("slc file io");
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "pathfinder-tests";
    std::error_code ec;
    fs::create_directories(dir, ec);

    Macro macro = makeTrickyMacro();
    fs::path path = dir / "roundtrip.slc";

    std::string error;
    CHECK(slc::saveToFile(path, macro, slc::Format::V3, error));
    CHECK(error.empty());
    CHECK(fs::exists(path));

    Macro loaded;
    CHECK(slc::loadFromFile(path, loaded, error));
    CHECK(error.empty());
    checkSameEvents(macro.events, loaded.events, "file");

    Macro missing;
    error.clear();
    CHECK(!slc::loadFromFile(dir / "does-not-exist.slc", missing, error));
    CHECK(!error.empty());

    CHECK(slc::defaultFileName(macro, slc::Format::V3).find(".slc") != std::string::npos);
    CHECK(slc::defaultFileName(macro, slc::Format::V3).find("Test_Level") != std::string::npos);

    fs::remove_all(dir, ec);
}

static void testDump() {
    SECTION("json and text dumps");
    Macro macro = makeTrickyMacro();
    macro.frameCount = macro.lastFrame();
    macro.notes = "a \"quoted\" note\nwith a newline";

    std::string const json = dump::toJson(macro);
    CHECK(json.find("\"pathfinder-macro\"") != std::string::npos);

    Macro decoded;
    std::string error;
    CHECK(dump::fromJson(json, decoded, error));
    CHECK(error.empty());
    checkSameEvents(macro.events, decoded.events, "dump");
    CHECK(decoded.notes == macro.notes);
    CHECK(decoded.levelName == macro.levelName);
    CHECK(decoded.tps == macro.tps);
    CHECK(decoded.frameCount == macro.frameCount);

    CHECK(!dump::toText(macro).empty());
    CHECK(dump::summary(macro).find("events") != std::string::npos);

    Macro bad;
    error.clear();
    CHECK(!dump::fromJson("{ definitely not a macro }", bad, error));
    CHECK(!error.empty());

    error.clear();
    CHECK(!dump::fromJson("{ \"format\": \"pathfinder-macro\" }", bad, error));
    CHECK(!error.empty());
}

// ---------------------------------------------------------------------------

static void testPlanGeneration() {
    SECTION("plan generation");
    SearchParams params;
    params.horizon = 12;
    params.maxPlansPerSegment = 64;
    params.holdFrames = 3;

    PlanContext context;
    auto plans = generatePlans(params, params.horizon, context);
    CHECK(!plans.empty());
    CHECK(plans.front().empty()); // coasting is always the first candidate
    CHECK(plans.size() <= params.maxPlansPerSegment);

    for (auto const& plan : plans) {
        for (auto const& in : plan) {
            CHECK(in.offset < params.horizon);
            CHECK(in.button == Button::Jump || in.button == Button::Left || in.button == Button::Right);
        }
    }

    // with left/right disabled there must be no directional candidates
    for (auto const& plan : plans) {
        for (auto const& in : plan) CHECK(in.button == Button::Jump);
    }

    SearchParams platformer = params;
    platformer.allowLeftRight = true;
    auto platformerPlans = generatePlans(platformer, params.horizon, context);
    bool sawDirectional = false;
    for (auto const& plan : platformerPlans) {
        for (auto const& in : plan) {
            if (in.button != Button::Jump) sawDirectional = true;
        }
    }
    CHECK(sawDirectional);

    // a tight budget still yields a usable, deduplicated set
    SearchParams tight = params;
    tight.maxPlansPerSegment = 6;
    auto tightPlans = generatePlans(tight, params.horizon, context);
    CHECK(tightPlans.size() <= 6);
    CHECK(tightPlans.front().empty());

    // carry over
    Plan hold;
    PlanInput press;
    press.offset = 2;
    press.button = Button::Jump;
    press.holding = true;
    hold.push_back(press);
    PlanContext carried;
    planCarryOver(hold, 12, carried);
    CHECK(carried.holdJump);

    Plan releasePlan;
    PlanInput release = press;
    release.offset = 5;
    release.holding = false;
    releasePlan.push_back(press);
    releasePlan.push_back(release);
    PlanContext released;
    released.holdJump = true;
    planCarryOver(releasePlan, 12, released);
    CHECK(!released.holdJump);

    // events are shifted by the base frame and mirrored in dual mode
    std::vector<MacroEvent> events;
    planToEvents(releasePlan, 100, false, events);
    CHECK(events.size() == 2);
    CHECK(events[0].frame == 102);
    CHECK(events[1].frame == 105);
    events.clear();
    planToEvents(releasePlan, 100, true, events);
    CHECK(events.size() == 4);
    CHECK(events[1].player2);
    CHECK(events[1].frame == 102);
}

// ---------------------------------------------------------------------------

static MockLevel makeJumpLevel() {
    MockLevel level;
    level.length = 1600.0;
    level.speed = 4.0;
    level.gravity = -0.9;
    level.jumpVelocity = 11.0;
    level.jumpBufferFrames = 4;
    // pits that have to be jumped over, spaced far enough apart to be fair
    level.pits = {
        {200.0, 216.0},
        {480.0, 496.0},
        {760.0, 780.0},
        {1040.0, 1056.0},
        {1320.0, 1340.0},
    };
    return level;
}

static void testSearchCompletes() {
    SECTION("search completes a jumpable level");
    MockLevel level = makeJumpLevel();
    MockSimulator sim(level);

    SearchParams params;
    params.horizon = 12;
    params.maxPlansPerSegment = 20;
    params.holdFrames = 3;
    params.backtrackDepth = 4;
    params.maxSimulatedFrames = 2'000'000;

    Searcher searcher(sim, params);
    std::string error;
    CHECK(searcher.start(error));
    CHECK(error.empty());

    SearchStatus status = SearchStatus::Running;
    int ticks = 0;
    while (status == SearchStatus::Running && ticks < 20000) {
        status = searcher.tick(500);
        searcher.showCommitted();
        ticks++;
    }

    CHECK(status == SearchStatus::Finished);
    CHECK(ticks < 20000);
    CHECK(!searcher.stats().partial);
    CHECK(searcher.macro().playerEventCount() > 0);
    CHECK(searcher.macro().tps == params.tps);

    // the committed macro must actually beat the level when replayed cold
    ReplayOutcome outcome = replayMacro(level, searcher.macro());
    CHECK(outcome.finished);
    CHECK(!outcome.died);

    // the macro has to survive an slc round trip too
    std::vector<uint8_t> bytes;
    CHECK(slc::encode(searcher.macro(), slc::Format::V3, bytes, error));
    Macro decoded;
    CHECK(slc::decode(bytes, decoded, error));
    ReplayOutcome afterRoundTrip = replayMacro(level, decoded);
    CHECK(afterRoundTrip.finished);

    // and the v2 container as well
    bytes.clear();
    CHECK(slc::encode(searcher.macro(), slc::Format::V2, bytes, error));
    Macro decodedV2;
    CHECK(slc::decode(bytes, decodedV2, error));
    CHECK(replayMacro(level, decodedV2).finished);

    CHECK(searcher.stats().simulatedFrames > 0);
    std::printf(
        "  info: %llu frames simulated, %llu segments, %llu candidates, %llu backtracks, %zu events\n",
        (unsigned long long)searcher.stats().simulatedFrames, (unsigned long long)searcher.stats().segmentsCommitted,
        (unsigned long long)searcher.stats().candidatesEvaluated, (unsigned long long)searcher.stats().backtracks,
        searcher.macro().events.size()
    );
}

static void testSearchWithDynamicState() {
    SECTION("search uses full restores for dynamic levels");
    MockLevel level = makeJumpLevel();
    MockSimulator sim(level);
    sim.setDynamic(true); // forces the expensive snapshot path

    SearchParams params;
    params.horizon = 12;
    params.maxPlansPerSegment = 16;
    params.backtrackDepth = 3;

    Searcher searcher(sim, params);
    std::string error;
    CHECK(searcher.start(error));

    SearchStatus status = SearchStatus::Running;
    int ticks = 0;
    while (status == SearchStatus::Running && ticks < 20000) {
        status = searcher.tick(500);
        searcher.showCommitted();
        ticks++;
    }
    CHECK(status == SearchStatus::Finished);
    CHECK(replayMacro(level, searcher.macro()).finished);
    // full restores happen once per candidate, so far more restores than with
    // the cheap path
    CHECK(sim.restoredSnapshots() > sim.resets());
}

static void testSearchNeedsBacktracking() {
    SECTION("search backtracks out of a delayed death trap");
    // Jumping over the first pit as late as possible is survivable for several
    // windows, but it puts the player in the air over the overhang further on.
    // The only way through is to jump early, which the greedy pass will not do
    // until backtracking forces it to.
    MockLevel level;
    level.length = 1200.0;
    level.speed = 4.0;
    level.gravity = -0.9;
    level.jumpVelocity = 11.0;
    level.jumpBufferFrames = 0; // no input buffering: timing has to be exact
    level.pits = {{200.0, 216.0}, {400.0, 416.0}};
    level.ceilings = {{280.0, 296.0}};

    MockSimulator sim(level);

    SearchParams params;
    params.horizon = 4; // 16 units per window, so the trap spans several windows
    params.maxPlansPerSegment = 16;
    params.holdFrames = 2;
    params.backtrackDepth = 12;
    params.maxBacktracks = 512;
    params.maxSimulatedFrames = 4'000'000;

    Searcher searcher(sim, params);
    std::string error;
    CHECK(searcher.start(error));
    CHECK(error.empty());

    SearchStatus status = SearchStatus::Running;
    int ticks = 0;
    while (status == SearchStatus::Running && ticks < 200000) {
        status = searcher.tick(2000);
        searcher.showCommitted();
        ticks++;
    }

    CHECK(status == SearchStatus::Finished);
    CHECK(searcher.stats().backtracks > 0); // the trap really was hit
    CHECK(!searcher.stats().partial);

    ReplayOutcome outcome = replayMacro(level, searcher.macro());
    CHECK(outcome.finished);
    CHECK(!outcome.died);

    std::printf(
        "  info: %llu frames simulated, %llu segments, %llu candidates, %llu backtracks, %zu events\n",
        (unsigned long long)searcher.stats().simulatedFrames, (unsigned long long)searcher.stats().segmentsCommitted,
        (unsigned long long)searcher.stats().candidatesEvaluated, (unsigned long long)searcher.stats().backtracks,
        searcher.macro().events.size()
    );
    CHECK(sim.liveSnapshots() == 0);
}

static void testSearchImpossible() {
    SECTION("search reports failure on an impossible level");
    MockLevel level;
    level.length = 1200.0;
    level.speed = 4.0;
    level.gravity = -0.9;
    level.jumpVelocity = 11.0;
    // a pit far wider than any jump: nothing can survive it
    level.pits = {{300.0, 900.0}};

    MockSimulator sim(level);
    SearchParams params;
    params.horizon = 12;
    params.maxPlansPerSegment = 16;
    params.backtrackDepth = 3;
    params.maxBacktracks = 64;

    Searcher searcher(sim, params);
    std::string error;
    CHECK(searcher.start(error));

    SearchStatus status = SearchStatus::Running;
    int ticks = 0;
    while (status == SearchStatus::Running && ticks < 20000) {
        status = searcher.tick(500);
        searcher.showCommitted();
        ticks++;
    }

    CHECK(status == SearchStatus::Failed);
    CHECK(!searcher.stats().stopReason.empty());
    CHECK(searcher.stats().partial);
    CHECK(ticks < 20000);
    std::printf("  info: stopped with '%s' at %.1f%%\n", searcher.stats().stopReason.c_str(),
                searcher.stats().bestProgress * 100.0);
}

static void testSearchCancelAndLeaks() {
    SECTION("search can be cancelled and does not leak snapshots");
    MockLevel level = makeJumpLevel();
    MockSimulator sim(level);

    SearchParams params;
    params.horizon = 12;
    params.maxPlansPerSegment = 16;
    params.backtrackDepth = 4;

    {
        Searcher searcher(sim, params);
        std::string error;
        CHECK(searcher.start(error));
        searcher.tick(2000);
        CHECK(searcher.status() == SearchStatus::Running);
        searcher.cancel();
        CHECK(searcher.status() == SearchStatus::Cancelled);
        // a cancelled search keeps the part it had already committed
        searcher.macro().normalize();
        for (size_t i = 1; i < searcher.macro().events.size(); i++) {
            CHECK(searcher.macro().events[i - 1].frame <= searcher.macro().events[i].frame);
        }
        // ticking after a cancel is a no-op
        CHECK(searcher.tick(1000) == SearchStatus::Cancelled);
    }

    // everything the search took must have been handed back
    CHECK(sim.liveSnapshots() == 0);
}

static void testSearchBudget() {
    SECTION("search respects its frame budget");
    MockLevel level = makeJumpLevel();
    MockSimulator sim(level);

    SearchParams params;
    params.horizon = 12;
    params.maxPlansPerSegment = 16;
    params.maxSimulatedFrames = 4000;

    Searcher searcher(sim, params);
    std::string error;
    CHECK(searcher.start(error));

    SearchStatus status = SearchStatus::Running;
    int ticks = 0;
    while (status == SearchStatus::Running && ticks < 1000) {
        status = searcher.tick(500);
        searcher.showCommitted();
        ticks++;
    }
    CHECK(status == SearchStatus::Failed);
    CHECK(searcher.stats().simulatedFrames <= params.maxSimulatedFrames + params.horizon);
    CHECK(searcher.stats().simulatedFrames >= params.maxSimulatedFrames);
}

static void testSearchAcrossTicks() {
    SECTION("search survives being split across many tiny ticks");
    MockLevel level = makeJumpLevel();
    MockSimulator sim(level);

    SearchParams params;
    params.horizon = 12;
    params.maxPlansPerSegment = 20;
    params.backtrackDepth = 6;

    Searcher searcher(sim, params);
    std::string error;
    CHECK(searcher.start(error));

    // One frame per tick, exactly like a very low "frames per tick" setting:
    // every candidate is interrupted and resumed many times.
    SearchStatus status = SearchStatus::Running;
    int ticks = 0;
    while (status == SearchStatus::Running && ticks < 2000000) {
        status = searcher.tick(1);
        searcher.showCommitted();
        ticks++;
    }

    CHECK(status == SearchStatus::Finished);
    CHECK(replayMacro(level, searcher.macro()).finished);
    std::printf("  info: %d ticks, %llu frames simulated\n", ticks,
                (unsigned long long)searcher.stats().simulatedFrames);
    CHECK(sim.liveSnapshots() == 0);
}

static void testSearchNotReady() {
    SECTION("search refuses a level that is not ready");
    MockSimulator sim;
    sim.setReady(false);
    Searcher searcher(sim);
    std::string error;
    CHECK(!searcher.start(error));
    CHECK(!error.empty());
    CHECK(searcher.status() == SearchStatus::NotStarted);
}

// ---------------------------------------------------------------------------

int main() {
    testHelpers();
    testByteIO();
    testV3RoundTrip();
    testV3Empty();
    testV2RoundTrip();
    testV3HandmadeSections();
    testDecodeErrors();
    testFileIO();
    testDump();
    testPlanGeneration();
    testSearchCompletes();
    testSearchWithDynamicState();
    testSearchNeedsBacktracking();
    testSearchImpossible();
    testSearchCancelAndLeaks();
    testSearchBudget();
    testSearchAcrossTicks();
    testSearchNotReady();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

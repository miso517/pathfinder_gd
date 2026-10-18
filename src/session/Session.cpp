#include "Session.hpp"

#include "../format/Dump.hpp"
#include "../sim/SimSignals.hpp"
#include "../ui/HudLayer.hpp"

#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

using namespace geode::prelude;

namespace pathfinder {

namespace {

    int64_t settingInt(char const* key, int64_t fallback) {
        auto* mod = Mod::get();
        if (!mod) return fallback;
        return mod->getSettingValue<int64_t>(key);
    }

    double settingDouble(char const* key, double fallback) {
        auto* mod = Mod::get();
        if (!mod) return fallback;
        double const value = mod->getSettingValue<double>(key);
        return std::isfinite(value) ? value : fallback;
    }

    bool settingBool(char const* key, bool fallback) {
        auto* mod = Mod::get();
        if (!mod) return fallback;
        return mod->getSettingValue<bool>(key);
    }

    std::string settingString(char const* key, char const* fallback) {
        auto* mod = Mod::get();
        if (!mod) return fallback;
        std::string const value = mod->getSettingValue<std::string>(key);
        return value.empty() ? std::string(fallback) : value;
    }

    int64_t clampi(int64_t value, int64_t low, int64_t high) { return std::max(low, std::min(high, value)); }

    std::string percent(double value) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.2f%%", value * 100.0);
        return buffer;
    }

    std::string frames(uint64_t frames, double tps) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%llu frames (%.1fs)", static_cast<unsigned long long>(frames),
                      tps > 0.0 ? static_cast<double>(frames) / tps : 0.0);
        return buffer;
    }

} // namespace

Session& Session::get() {
    static Session instance;
    return instance;
}

Session::~Session() { cleanup(); }

// ---------------------------------------------------------------- level hooks

void Session::onPlayLayerInit(PlayLayer* layer) {
    cleanup();
    m_layer = layer;
    m_macro = Macro{};
    m_stats = SearchStats{};
    applyLevelInfo();
}

void Session::onPlayLayerExit(PlayLayer* layer) {
    if (m_layer && m_layer != layer) return;
    m_hud = nullptr;
    cleanup();
    m_layer = nullptr;
    m_macro = Macro{};
    m_stats = SearchStats{};
}

bool Session::wantsDrive(GJBaseGameLayer* layer) const {
    if (!layer || layer != m_layer) return false;
    return m_mode == Mode::Searching || m_mode == Mode::Running || m_mode == Mode::Rewinding;
}

void Session::drive(PlayLayer* layer, float dt) {
    if (layer != m_layer || !m_sim) {
        m_mode = Mode::Idle;
        return;
    }

    switch (m_mode) {
        case Mode::Searching: {
            if (!m_searcher) {
                m_mode = Mode::Rewinding;
                return;
            }
            // Dual mode can be switched on mid level by a portal, so the search
            // is told about it every frame instead of only at the start.
            m_searcher->params().dualMode = m_sim->dualMode();

            uint64_t const budget = static_cast<uint64_t>(clampi(settingInt("frames-per-tick", 800), 8, 200000));
            SearchStatus const status = m_searcher->tick(budget);
            // Put the frontier back on screen so the player watches the search
            // progress instead of a half simulated candidate.
            m_searcher->showCommitted();
            m_stats = m_searcher->stats();
            refreshHud();

            if (status != SearchStatus::Running) finishSearch(status);
            break;
        }

        case Mode::Running: {
            uint32_t const steps = playbackStepsPerFrame(dt);
            for (uint32_t i = 0; i < steps && m_mode == Mode::Running; i++) {
                if (!stepPlayback()) break;
            }
            refreshHud();
            break;
        }

        case Mode::Rewinding: {
            rewindToStart();
            exitSandbox();
            m_mode = Mode::Idle;
            refreshHud();
            break;
        }

        default: break;
    }
}

void Session::afterUpdate(PlayLayer* layer, float dt) {
    if (layer != m_layer) return;

    if (m_mode == Mode::Recording) {
        m_recordFrame++;
        m_recordSeconds += dt;
    }
    refreshHud();
}

void Session::onButton(bool down, int button, bool isPlayer1) {
    if (m_mode != Mode::Recording) return;
    if (button < static_cast<int>(Button::Jump) || button > static_cast<int>(Button::Right)) return;

    MacroEvent event;
    event.frame = m_recordFrame;
    event.type = static_cast<EventType>(static_cast<uint8_t>(button));
    event.holding = down;
    event.player2 = !isPlayer1;
    m_recording.events.push_back(event);
}

// ------------------------------------------------------------ user actions

void Session::startSearch() {
    if (m_mode != Mode::Idle) {
        notify("Pathfinder is busy - stop it first", geode::NotificationIcon::Warning);
        return;
    }
    if (!ensureSimulator()) return;

    applyLevelInfo();
    m_stats = SearchStats{};
    beginRestart(Pending::Search);
}

void Session::stop() {
    switch (m_mode) {
        case Mode::Searching:
            if (m_searcher) m_searcher->cancel();
            finishSearch(SearchStatus::Cancelled);
            break;
        case Mode::Running: endPlayback(false, "stopped"); break;
        case Mode::Recording: stopRecording(); break;
        case Mode::Restarting:
            m_pending = Pending::None;
            m_mode = Mode::Rewinding;
            break;
        default: break;
    }
}

void Session::runMacro() {
    if (m_mode != Mode::Idle) {
        notify("Pathfinder is busy - stop it first", geode::NotificationIcon::Warning);
        return;
    }
    if (m_macro.empty()) {
        notify("There is no macro to play yet", geode::NotificationIcon::Warning);
        return;
    }
    if (!ensureSimulator()) return;

    m_sim->setTps(m_macro.tps);
    beginRestart(Pending::Run);
}

void Session::startRecording() {
    if (m_mode != Mode::Idle) {
        notify("Pathfinder is busy - stop it first", geode::NotificationIcon::Warning);
        return;
    }
    if (!m_layer) {
        notify("Open a level first", geode::NotificationIcon::Warning);
        return;
    }

    m_recording = Macro{};
    m_recording.tps = tps();
    m_recording.source = "record";
    m_recording.levelName = m_macro.levelName;
    m_recording.levelID = m_macro.levelID;
    m_recording.platformer = m_macro.platformer;
    m_recordFrame = 0;
    m_recordSeconds = 0.0;
    m_mode = Mode::Recording;
    notify("Recording inputs - play the level, then press stop", geode::NotificationIcon::Loading);
}

void Session::stopRecording() {
    if (m_mode != Mode::Recording) return;

    // The recorded frames are the game's own frames, so the macro's tps is
    // simply the rate they were produced at.
    if (m_recordSeconds > 0.05 && m_recordFrame > 4) m_recording.tps = static_cast<double>(m_recordFrame) / m_recordSeconds;
    m_recording.frameCount = m_recordFrame;
    m_recording.normalize();
    m_macro = m_recording;
    applyLevelInfo();
    m_mode = Mode::Idle;

    if (m_macro.empty()) {
        notify("Nothing was recorded", geode::NotificationIcon::Warning);
    } else {
        notify("Recorded " + dump::summary(m_macro), geode::NotificationIcon::Success);
    }
    refreshHud();
}

// ------------------------------------------------------------ sandbox

bool Session::ensureSimulator() {
    if (!m_layer) {
        notify("Open a level first", geode::NotificationIcon::Warning);
        return false;
    }
    if (!m_layer->m_player1) {
        notify("The level is still loading", geode::NotificationIcon::Warning);
        return false;
    }
    if (!m_sim) {
        m_sim = std::make_unique<GDSimulator>(m_layer, tps());
        sim::active() = m_sim.get();
    }
    m_sim->setTps(tps());
    return true;
}

void Session::enterSandbox() {
    if (!m_layer) return;

    // Checkpoints are practice mode machinery, and running the search in
    // practice mode also keeps attempts and statistics untouched.
    m_practiceWasOn = m_layer->m_isPracticeMode;
    if (!m_practiceWasOn) {
        m_layer->togglePracticeMode(true);
        m_practiceChanged = true;
    }

    if (settingBool("mute-while-running", true) && !m_muted) {
        if (auto* manager = GameManager::sharedState()) {
            m_savedMusicVolume = manager->m_bgVolume;
            m_savedEffectsVolume = manager->m_sfxVolume;
        }
        if (auto* audio = FMODAudioEngine::sharedEngine()) {
            audio->setBackgroundMusicVolume(0.f);
            audio->setEffectsVolume(0.f);
        }
        m_muted = true;
    }
}

void Session::exitSandbox() {
    if (m_muted) {
        if (auto* audio = FMODAudioEngine::sharedEngine()) {
            audio->setBackgroundMusicVolume(m_savedMusicVolume);
            audio->setEffectsVolume(m_savedEffectsVolume);
        }
        m_muted = false;
    }
    if (m_practiceChanged && m_layer) {
        m_layer->togglePracticeMode(m_practiceWasOn);
        m_practiceChanged = false;
    }
}

void Session::beginRestart(Pending pending) {
    m_pending = pending;
    enterSandbox();
    m_mode = Mode::Restarting;
    m_restartCountdown = 4;
    if (m_layer) m_layer->resetLevelFromStart();
    refreshHud();
}

SearchParams Session::buildParams() {
    SearchParams params;
    params.tps = tps();
    params.horizon = static_cast<uint32_t>(clampi(settingInt("horizon", 16), 2, 256));
    params.maxPlansPerSegment = static_cast<uint32_t>(clampi(settingInt("candidates", 24), 2, 512));
    params.holdFrames = static_cast<uint32_t>(clampi(settingInt("hold-frames", 3), 1, 64));
    params.maxSimulatedFrames = static_cast<uint64_t>(clampi(settingInt("frame-budget", 6000000), 10000, 400000000));
    params.backtrackDepth = static_cast<uint32_t>(clampi(settingInt("backtrack-depth", 6), 1, 128));
    params.maxBacktracks = static_cast<uint32_t>(clampi(settingInt("max-backtracks", 512), 1, 100000));
    params.useCheapRestore = settingBool("fast-snapshots", true);
    params.deathPenalty = settingDouble("death-penalty", 0.25);

    if (m_sim) {
        params.allowLeftRight = m_sim->platformer();
        params.dualMode = m_sim->dualMode();
    }
    if (m_layer && m_layer->m_level) {
        params.seed = static_cast<uint64_t>(static_cast<int>(m_layer->m_level->m_platformerSeed));
    }
    return params;
}

void Session::beginSearch() {
    if (!m_sim) {
        m_mode = Mode::Idle;
        exitSandbox();
        return;
    }

    if (!m_sim->captureRoot()) {
        notify("Could not snapshot the level start", geode::NotificationIcon::Error);
        m_mode = Mode::Rewinding;
        return;
    }

    m_searcher = std::make_unique<Searcher>(*m_sim, buildParams());

    std::string error;
    if (!m_searcher->start(error)) {
        notify("Could not start the search: " + error, geode::NotificationIcon::Error);
        m_searcher.reset();
        m_mode = Mode::Rewinding;
        return;
    }

    m_stats = m_searcher->stats();
    m_mode = Mode::Searching;
    notify("Searching for a survivable path…", geode::NotificationIcon::Loading);
    log::info(
        "pathfinder: started search (tps {}, horizon {}, candidates {}, dynamic state {})", m_searcher->params().tps,
        m_searcher->params().horizon, m_searcher->params().maxPlansPerSegment, m_sim->hasDynamicState()
    );
    refreshHud();
}

void Session::beginRun() {
    if (!m_sim) {
        m_mode = Mode::Idle;
        exitSandbox();
        return;
    }
    if (!m_sim->captureRoot()) {
        notify("Could not snapshot the level start", geode::NotificationIcon::Error);
        m_mode = Mode::Rewinding;
        return;
    }

    m_playCursor = 0;
    m_playFrame = 0;
    m_stats = SearchStats{};
    m_stats.partial = false;
    m_mode = Mode::Running;
    notify("Playing macro…", geode::NotificationIcon::Loading);
    refreshHud();
}

void Session::finishSearch(SearchStatus status) {
    if (m_searcher) {
        m_stats = m_searcher->stats();
        m_macro = m_searcher->macro();
        m_macro.normalize();
        m_searcher.reset();
    }
    applyLevelInfo();

    switch (status) {
        case SearchStatus::Finished:
            notify("Path found: " + dump::summary(m_macro), geode::NotificationIcon::Success);
            break;
        case SearchStatus::Cancelled: notify("Search stopped at " + percent(m_stats.bestProgress), geode::NotificationIcon::Warning); break;
        case SearchStatus::Failed:
            notify(
                m_macro.empty() ? ("No path found: " + m_stats.stopReason)
                                : ("Stopped at " + percent(m_stats.bestProgress) + ": " + m_stats.stopReason),
                m_macro.empty() ? geode::NotificationIcon::Error : geode::NotificationIcon::Warning
            );
            break;
        default: break;
    }

    log::info(
        "pathfinder: search ended ({}): {} simulated, {} segments committed, {} backtracks, {} events", statusText(),
        m_stats.simulatedFrames, m_stats.segmentsCommitted, m_stats.backtracks, m_macro.events.size()
    );

    sim::active() = nullptr;
    m_mode = Mode::Rewinding;
    refreshHud();
}

void Session::rewindToStart() {
    if (!m_sim) return;
    // Puts the player back at the beginning of the level and drops every
    // snapshot the search was holding.
    m_sim->releaseAll();
    m_sim->reset();
}

void Session::endPlayback(bool success, std::string reason) {
    m_stats.committedProgress = m_sim ? m_sim->progress() : 0.0;
    m_stats.partial = !success;
    m_stats.stopReason = reason;
    m_mode = Mode::Rewinding;
    notify(
        success ? ("Macro verified: " + reason) : ("Macro failed: " + reason),
        success ? geode::NotificationIcon::Success : geode::NotificationIcon::Warning
    );
    refreshHud();
}

uint32_t Session::playbackStepsPerFrame(float dt) const {
    double const speed = settingDouble("playback-speed", 1.0);
    double const frameSeconds = (dt > 0.0001f) ? static_cast<double>(dt) : (1.0 / 60.0);
    double const tps = m_sim ? m_sim->tps() : 240.0;

    // At speed 1 the macro plays in real time: tps ticks per second of game time.
    double steps = tps * frameSeconds * speed;
    if (!(steps >= 1.0)) steps = 1.0;
    if (steps > 4000.0) steps = 4000.0;
    return static_cast<uint32_t>(steps);
}

bool Session::stepPlayback() {
    if (!m_sim || m_macro.empty()) return false;

    Plan plan;
    while (m_playCursor < m_macro.events.size() && m_macro.events[m_playCursor].frame <= m_playFrame) {
        MacroEvent const& event = m_macro.events[m_playCursor++];
        if (isPlayerEvent(event.type)) {
            PlanInput input;
            input.offset = 0;
            input.button = buttonOf(event.type);
            input.holding = event.holding;
            input.player2 = event.player2;
            plan.push_back(input);
        } else if (event.type == EventType::TPS) {
            m_sim->setTps(event.tps);
        } else if (event.type == EventType::Restart || event.type == EventType::RestartFull ||
                   event.type == EventType::Death) {
            // Bugpoint style macro: rewind to the level start and carry on from
            // the frame the restart was recorded on.
            m_sim->reset();
            m_playFrame = event.frame;
            return true;
        }
    }

    m_sim->beginWindow(plan);
    uint32_t const stepped = m_sim->step(1);
    m_playFrame++;

    double const progress = m_sim->progress();
    if (progress > m_stats.committedProgress) m_stats.committedProgress = progress;
    if (progress > m_stats.bestProgress) m_stats.bestProgress = progress;

    if (stepped == 0 || !m_sim->alive()) {
        endPlayback(false, "died at " + percent(progress) + " on " + frames(m_playFrame, m_sim->tps()));
        return false;
    }
    if (m_sim->finished()) {
        endPlayback(true, "level completed in " + frames(m_playFrame, m_sim->tps()));
        return false;
    }
    if (m_playCursor >= m_macro.events.size() && m_playFrame > m_macro.frameCount + 240) {
        endPlayback(progress > 0.99, "the macro ran out at " + percent(progress));
        return false;
    }
    return true;
}

void Session::cleanup() {
    if (m_searcher) {
        m_searcher->abort();
        m_searcher.reset();
    }
    if (m_sim) {
        if (sim::active() == m_sim.get()) sim::active() = nullptr;
        m_sim.reset();
    }
    if (m_mode == Mode::Searching || m_mode == Mode::Running || m_mode == Mode::Restarting) exitSandbox();
    m_mode = Mode::Idle;
    m_pending = Pending::None;
}

// ------------------------------------------------------------ import / export

slc::Format Session::exportFormat() const {
    return settingString("export-format", "slc v3") == "slc v2" ? slc::Format::V2 : slc::Format::V3;
}

double Session::tps() const {
    double const value = settingDouble("tps", 240.0);
    return value < 30.0 ? 30.0 : (value > 2000.0 ? 2000.0 : value);
}

bool Session::writeMacro(std::filesystem::path const& path) {
    applyLevelInfo();

    std::string error;
    if (!slc::saveToFile(path, m_macro, exportFormat(), error)) {
        notify("Could not save the macro: " + error, geode::NotificationIcon::Error);
        log::error("pathfinder: failed to write {}: {}", path.string(), error);
        return false;
    }
    notify("Saved " + path.filename().string(), geode::NotificationIcon::Success);
    log::info("pathfinder: wrote {} ({} events)", path.string(), m_macro.events.size());
    return true;
}

void Session::exportMacro() {
    if (m_macro.empty()) {
        notify("There is no macro to export yet", geode::NotificationIcon::Warning);
        return;
    }

    file::FilePickOptions options;
    options.defaultPath = Mod::get()->getSaveDir() / slc::defaultFileName(m_macro, exportFormat());
    file::FilePickOptions::Filter filter;
    filter.description = "Silicate macro";
    filter.files = {".slc"};
    options.filters.push_back(filter);

    async::spawn(
        file::pick(file::PickMode::SaveFile, options), [this](Result<std::optional<std::filesystem::path>> result) {
            if (!result.isOk()) {
                notify("The file picker failed", geode::NotificationIcon::Error);
                return;
            }
            auto picked = result.unwrap();
            if (!picked) return; // the user cancelled
            this->writeMacro(*picked);
        }
    );
}

void Session::exportToSaveDir() {
    if (m_macro.empty()) {
        notify("There is no macro to export yet", geode::NotificationIcon::Warning);
        return;
    }
    std::filesystem::path const path = Mod::get()->getSaveDir() / slc::defaultFileName(m_macro, exportFormat());
    writeMacro(path);
    file::openFolder(path.parent_path());
}

void Session::exportDump() {
    if (m_macro.empty()) {
        notify("There is no macro to export yet", geode::NotificationIcon::Warning);
        return;
    }

    applyLevelInfo();
    std::filesystem::path const dir = Mod::get()->getSaveDir();
    std::filesystem::path const base = dir / slc::defaultFileName(m_macro, exportFormat());
    base.replace_extension();

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::string const json = dump::toJson(m_macro);
    std::string const text = dump::toText(m_macro);

    std::filesystem::path const jsonPath = base;
    std::filesystem::path const textPath = base;
    bool ok = writeFile(jsonPath.replace_extension(".json"), json);
    ok = writeFile(textPath.replace_extension(".txt"), text) && ok;

    if (ok) {
        notify("Wrote " + jsonPath.filename().string() + " and " + textPath.filename().string(),
               geode::NotificationIcon::Success);
        file::openFolder(dir);
    } else {
        notify("Could not write the dump files", geode::NotificationIcon::Error);
    }
}

bool Session::writeFile(std::filesystem::path const& path, std::string const& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        log::error("pathfinder: could not open {} for writing", path.string());
        return false;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(file);
}

void Session::importMacro() {
    file::FilePickOptions options;
    options.defaultPath = Mod::get()->getSaveDir();
    file::FilePickOptions::Filter filter;
    filter.description = "Silicate macro";
    filter.files = {".slc"};
    options.filters.push_back(filter);

    async::spawn(
        file::pick(file::PickMode::OpenFile, options), [this](Result<std::optional<std::filesystem::path>> result) {
            if (!result.isOk()) {
                notify("The file picker failed", geode::NotificationIcon::Error);
                return;
            }
            auto picked = result.unwrap();
            if (!picked) return; // the user cancelled

            Macro loaded;
            std::string error;
            if (!slc::loadFromFile(*picked, loaded, error)) {
                notify("Could not read that macro: " + error, geode::NotificationIcon::Error);
                return;
            }
            loaded.levelName = loaded.levelName.empty() ? picked->stem().string() : loaded.levelName;
            this->m_macro = std::move(loaded);
            this->m_stats = SearchStats{};
            this->m_stats.partial = false;
            this->applyLevelInfo();
            notify("Loaded " + dump::summary(this->m_macro), geode::NotificationIcon::Success);
            this->refreshHud();
        }
    );
}

void Session::openSaveFolder() {
    std::filesystem::path const dir = Mod::get()->getSaveDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!file::openFolder(dir)) notify("Could not open " + dir.string(), geode::NotificationIcon::Warning);
}

// ------------------------------------------------------------ presentation

void Session::applyLevelInfo() {
    if (!m_layer || !m_layer->m_level) return;
    GJGameLevel* level = m_layer->m_level;
    m_macro.levelName = std::string(level->m_levelName);
    m_macro.levelID = static_cast<int>(level->m_levelID);
    m_macro.platformer = level->isPlatformer();
    if (m_macro.tps <= 0.0) m_macro.tps = tps();
}

double Session::progress() const {
    if (m_mode == Mode::Searching && m_searcher) return m_searcher->committedProgress();
    if (m_sim && (m_mode == Mode::Running)) return m_sim->progress();
    return m_stats.committedProgress;
}

std::string Session::statusText() const {
    switch (m_mode) {
        case Mode::Idle:
            return m_macro.empty() ? "idle - no macro" : ("macro ready: " + std::to_string(m_macro.events.size()) + " events");
        case Mode::Restarting: return "restarting level…";
        case Mode::Searching: {
            char buffer[192];
            std::snprintf(
                buffer, sizeof(buffer), "searching %s | %s | %llu backtracks", percent(m_stats.committedProgress).c_str(),
                frames(m_stats.simulatedFrames, tps()).c_str(), static_cast<unsigned long long>(m_stats.backtracks)
            );
            return buffer;
        }
        case Mode::Running: return "playing macro " + percent(m_stats.committedProgress);
        case Mode::Recording:
            return "recording " + frames(m_recordFrame, m_recording.tps) + " | " +
                   std::to_string(m_recording.events.size()) + " events";
        case Mode::Rewinding: return "rewinding…";
    }
    return "idle";
}

std::string Session::detailText() const {
    if (m_sim) return m_sim->describe();
    return std::string();
}

void Session::notify(std::string const& text, NotificationIcon icon) {
    Notification::create(text, icon)->show();
}

void Session::attachHud(HudLayer* hud) {
    m_hud = hud;
    refreshHud();
}

void Session::detachHud(HudLayer* hud) {
    if (m_hud == hud) m_hud = nullptr;
}

void Session::refreshHud() {
    if (m_hud) m_hud->refresh();
}

} // namespace pathfinder

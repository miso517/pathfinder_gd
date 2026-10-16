#pragma once

// Central controller for everything the mod does inside a level.
//
// The Geode hooks in src/hooks are deliberately thin: they forward to this
// singleton, which owns the simulator, the running search, macro playback and
// input recording. There is only ever one PlayLayer alive, so a single session
// object is enough.

#include <Geode/Bindings.hpp>
#include <Geode/ui/Notification.hpp>

#include "../core/Search.hpp"
#include "../core/Types.hpp"
#include "../format/Slc.hpp"
#include "../sim/GDSimulator.hpp"

#include <memory>
#include <string>

using namespace geode::prelude;

namespace pathfinder {

class HudLayer;

class Session {
public:
    enum class Mode {
        Idle,       ///< the game runs normally
        Restarting, ///< waiting for the level to come back to its start
        Searching,  ///< the pathfinder is driving the engine
        Running,    ///< a macro is being played back
        Recording,  ///< the player's own inputs are being recorded
        Rewinding,  ///< putting the level back before handing control back
    };

    /// What should happen once the level has been rewound to its start.
    enum class Pending {
        None,
        Search,
        Run,
    };

    static Session& get();

    Session(Session const&) = delete;
    Session& operator=(Session const&) = delete;

    // ---- hook entry points --------------------------------------------------
    void onPlayLayerInit(PlayLayer* layer);
    void onPlayLayerExit(PlayLayer* layer);

    /// True when the mod should take over this frame instead of letting the game
    /// advance by its own variable `dt`.
    bool wantsDrive(GJBaseGameLayer* layer) const;
    /// Runs one rendered frame worth of simulation. `dt` is the real frame time,
    /// which is what macro playback uses to stay in real time at speed 1.
    void drive(PlayLayer* layer, float dt);
    /// Runs after a normal (not driven) game update.
    void afterUpdate(PlayLayer* layer, float dt);
    /// A button changed state; used while recording.
    void onButton(bool down, int button, bool isPlayer1);

    // ---- user actions -------------------------------------------------------
    void startSearch();
    void stop();
    void runMacro();
    void startRecording();
    void stopRecording();

    void exportMacro();
    void exportToSaveDir();
    void exportDump();
    void importMacro();
    void openSaveFolder();

    // ---- state --------------------------------------------------------------
    Mode mode() const { return m_mode; }
    bool driving() const { return m_mode == Mode::Searching || m_mode == Mode::Running; }
    bool busy() const { return m_mode != Mode::Idle; }

    Macro& macro() { return m_macro; }
    Macro const& macro() const { return m_macro; }
    SearchStats const& stats() const { return m_stats; }

    PlayLayer* layer() const { return m_layer; }
    GDSimulator* simulator() const { return m_sim.get(); }
    Searcher* searcher() const { return m_searcher.get(); }

    std::string statusText() const;
    std::string detailText() const;
    double progress() const;

    void attachHud(HudLayer* hud);
    void detachHud(HudLayer* hud);
    void refreshHud();
    void notify(std::string const& text, geode::NotificationIcon icon = geode::NotificationIcon::Info);

    slc::Format exportFormat() const;
    double tps() const;

private:
    Session() = default;
    ~Session();

    SearchParams buildParams();
    bool ensureSimulator();
    void enterSandbox();
    void exitSandbox();
    void beginRestart(Pending pending);
    void beginSearch();
    void beginRun();
    void finishSearch(SearchStatus status);
    void rewindToStart();
    void cleanup();

    uint32_t playbackStepsPerFrame(float dt) const;
    bool stepPlayback();
    void endPlayback(bool success, std::string reason);

    bool writeMacro(std::filesystem::path const& path);
    static bool writeFile(std::filesystem::path const& path, std::string const& contents);
    void applyLevelInfo();

    PlayLayer* m_layer = nullptr;
    HudLayer* m_hud = nullptr;

    std::unique_ptr<GDSimulator> m_sim;
    std::unique_ptr<Searcher> m_searcher;

    Mode m_mode = Mode::Idle;
    Pending m_pending = Pending::None;
    int m_restartCountdown = 0;

    Macro m_macro;
    SearchStats m_stats;

    // playback
    size_t m_playCursor = 0;
    uint64_t m_playFrame = 0;

    // recording
    Macro m_recording;
    uint64_t m_recordFrame = 0;
    double m_recordSeconds = 0.0;

    // sandbox bookkeeping
    bool m_practiceWasOn = false;
    bool m_practiceChanged = false;
    bool m_muted = false;
    float m_savedMusicVolume = 1.f;
    float m_savedEffectsVolume = 1.f;
    bool m_deferredRewind = false;
};

} // namespace pathfinder

// Pathfinder for Geometry Dash
//
// Finds a survivable path through a level by searching the *real* game engine
// frame by frame, then exports it as a Silicate `.slc` macro.
//
// See about.md for the user facing description and README.md for the design.

#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>

#include "session/Session.hpp"
#include "ui/PathfinderPopup.hpp"

using namespace geode::prelude;

$on_game(Loaded) {
    auto& session = pathfinder::Session::get();

    listenForKeybindSettingPresses("open-menu", [](Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) pathfinder::openPathfinderPopup();
    });

    listenForKeybindSettingPresses("find-path", [&session](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        if (session.mode() == pathfinder::Session::Mode::Idle) session.startSearch();
        else session.stop();
    });

    listenForKeybindSettingPresses("stop", [&session](Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) session.stop();
    });

    log::info("Pathfinder ready: press the configured keybind inside a level, or use the HUD button");
}

// Every hook the mod needs, in one place.
//
// The hooks are intentionally thin. They only decide *whether* the mod should
// take over a frame and forward everything else to the session, so the
// interesting logic stays testable.

#include <Geode/Bindings.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/utils/casts.hpp>

#include "../session/Session.hpp"
#include "../sim/SimSignals.hpp"
#include "../ui/HudLayer.hpp"

using namespace geode::prelude;

namespace pathfinder {

    /// The simulator that is currently driving the game, if any.
    static GDSimulator* activeSimulator() { return sim::active(); }

} // namespace pathfinder

class $modify(PFPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        pathfinder::Session::get().onPlayLayerInit(this);

        if (auto* hud = pathfinder::HudLayer::create(this)) {
            // High enough to sit above the gameplay layers, low enough that the
            // pause menu and Geode popups still cover it.
            this->addChild(hud, 100);
        }

        return true;
    }

    void onExit() {
        pathfinder::Session::get().onPlayLayerExit(this);
        PlayLayer::onExit();
    }

    void levelComplete() {
        // Finishing the level during a search or a verification run must not
        // trigger the real end-of-level sequence: no rewards, no stats and no
        // end layer. The run is reported by the mod instead.
        if (auto* simulator = pathfinder::activeSimulator()) {
            if (simulator->suppressEffects()) {
                simulator->notifyFinish();
                return;
            }
        }
        PlayLayer::levelComplete();
    }
};

class $modify(PFBaseGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        // We are in the middle of stepping the engine ourselves: just run the
        // original update and do not let the session drive recursively.
        if (pathfinder::sim::stepping()) {
            GJBaseGameLayer::update(dt);
            return;
        }

        auto& session = pathfinder::Session::get();
        auto* playLayer = typeinfo_cast<PlayLayer*>(this);

        if (playLayer && session.wantsDrive(this)) {
            // Take the frame over completely: the game is advanced by a fixed
            // number of ticks at the macro's tps instead of by the variable
            // render delta.
            session.drive(playLayer, dt);
            return;
        }

        GJBaseGameLayer::update(dt);

        if (playLayer) session.afterUpdate(playLayer, dt);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        // Used while recording the player's own run into a macro. The simulator
        // injects its inputs through this same function, which is exactly what
        // makes a recorded macro and a searched macro equivalent.
        pathfinder::Session::get().onButton(down, button, isPlayer1);
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(PFPlayerObject, PlayerObject) {
    void playerDestroyed(bool noEffects) {
        if (auto* simulator = pathfinder::activeSimulator()) {
            if (simulator->suppressEffects()) {
                // A death inside a simulation is information, not a game over:
                // no explosion, no sound and no retry layer.
                simulator->notifyDeath(this);
                return;
            }
        }
        PlayerObject::playerDestroyed(noEffects);
    }
};

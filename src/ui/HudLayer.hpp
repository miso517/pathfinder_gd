#pragma once

// In-level status readout.
//
// The search runs while the level is being driven frame by frame, so the player
// needs to see what is happening without opening a popup: current progress,
// simulated frames, and a stop button that is always reachable.

#include <Geode/Bindings.hpp>

using namespace geode::prelude;

namespace pathfinder {

class HudLayer : public cocos2d::CCNode {
public:
    static HudLayer* create(PlayLayer* layer);
    ~HudLayer() override;

    bool init(PlayLayer* layer);

    /// Pulls the current state out of the session and updates the labels.
    void refresh();

    void setVisibleHud(bool visible);

private:
    void onMenu(CCObject* sender);
    void onStop(CCObject* sender);

    PlayLayer* m_layer = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_detailLabel = nullptr;
    CCMenuItemSpriteExtra* m_stopButton = nullptr;
    CCMenuItemSpriteExtra* m_menuButton = nullptr;
};

} // namespace pathfinder

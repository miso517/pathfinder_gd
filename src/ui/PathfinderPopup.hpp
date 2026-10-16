#pragma once

// The main control panel: start a search, stop it, play a macro back, record
// your own run, and move macros in and out of the mod as .slc files.

#include <Geode/Bindings.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

namespace pathfinder {

/// Opens the popup (used by the HUD, the pause menu button and the keybind).
void openPathfinderPopup();

class PathfinderPopup : public geode::Popup {
public:
    static PathfinderPopup* create();

    bool init();
    void update(float dt) override;

private:
    void refresh();

    CCMenuItemSpriteExtra* makeButton(char const* caption, float width, SEL_MenuHandler selector);

    void onFind(CCObject* sender);
    void onStop(CCObject* sender);
    void onPlay(CCObject* sender);
    void onRecord(CCObject* sender);
    void onImport(CCObject* sender);
    void onExport(CCObject* sender);
    void onQuickExport(CCObject* sender);
    void onDump(CCObject* sender);
    void onFolder(CCObject* sender);
    void onSettings(CCObject* sender);
    void onAbout(CCObject* sender);

    cocos2d::CCLabelBMFont* m_levelLabel = nullptr;
    cocos2d::CCLabelBMFont* m_macroLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;

    CCMenuItemSpriteExtra* m_findButton = nullptr;
    CCMenuItemSpriteExtra* m_stopButton = nullptr;
    CCMenuItemSpriteExtra* m_playButton = nullptr;
    CCMenuItemSpriteExtra* m_recordButton = nullptr;
    CCMenuItemSpriteExtra* m_importButton = nullptr;
    CCMenuItemSpriteExtra* m_exportButton = nullptr;
    CCMenuItemSpriteExtra* m_quickExportButton = nullptr;
    CCMenuItemSpriteExtra* m_dumpButton = nullptr;

    ButtonSprite* m_recordSprite = nullptr;
};

} // namespace pathfinder

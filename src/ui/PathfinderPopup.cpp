#include "PathfinderPopup.hpp"

#include "../format/Dump.hpp"
#include "../session/Session.hpp"

#include <Geode/loader/Mod.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/cocos.hpp>

#include <cstdio>

using namespace geode::prelude;

namespace pathfinder {

namespace {
    constexpr float POPUP_WIDTH = 400.f;
    constexpr float POPUP_HEIGHT = 312.f;

    // button centres, three per row
    constexpr float COL_LEFT = 95.f;
    constexpr float COL_MIDDLE = 200.f;
    constexpr float COL_RIGHT = 305.f;

    constexpr float ROW_1 = 196.f;
    constexpr float ROW_2 = 158.f;
    constexpr float ROW_3 = 120.f;
    constexpr float ROW_4 = 82.f;
} // namespace

void openPathfinderPopup() {
    if (auto* popup = PathfinderPopup::create()) popup->show();
}

PathfinderPopup* PathfinderPopup::create() {
    auto* ret = new PathfinderPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

CCMenuItemSpriteExtra* PathfinderPopup::makeButton(char const* caption, float width, SEL_MenuHandler selector) {
    auto* sprite = ButtonSprite::create(caption, static_cast<int>(width), true, "goldFont.fnt", "GJ_button_01.png", 0.f,
                                        width > 110.f ? 0.6f : 0.65f);
    auto* button = CCMenuItemSpriteExtra::create(sprite, this, selector);
    button->setSizeMult(1.6f);
    return button;
}

bool PathfinderPopup::init() {
    if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT)) return false;

    this->setTitle("Pathfinder");
    this->scheduleUpdate();

    m_levelLabel = CCLabelBMFont::create("no level", "bigFont.fnt");
    m_levelLabel->setScale(0.45f);
    m_mainLayer->addChildAtPosition(m_levelLabel, Anchor::Top, CCPoint{0.f, -22.f});

    m_macroLabel = CCLabelBMFont::create("no macro", "chatFont.fnt");
    m_macroLabel->setScale(0.7f);
    m_mainLayer->addChildAtPosition(m_macroLabel, Anchor::Top, CCPoint{0.f, -40.f});

    m_statusLabel = CCLabelBMFont::create("idle", "chatFont.fnt");
    m_statusLabel->setScale(0.6f);
    m_statusLabel->setAlignment(cocos2d::kCCTextAlignmentCenter);
    m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::Top, CCPoint{0.f, -60.f});

    m_hintLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_hintLabel->setScale(0.55f);
    m_hintLabel->setAlignment(cocos2d::kCCTextAlignmentCenter);
    m_hintLabel->setColor(ccc3(190, 190, 190));
    m_mainLayer->addChildAtPosition(m_hintLabel, Anchor::Bottom, CCPoint{0.f, 34.f});

    auto* menu = CCMenu::create();
    menu->setContentSize(CCSize{POPUP_WIDTH, POPUP_HEIGHT});
    m_mainLayer->addChildAtPosition(menu, Anchor::Center);

    m_findButton = makeButton("Find Path", 120.f, menu_selector(PathfinderPopup::onFind));
    m_findButton->setPosition(CCPoint{COL_LEFT, ROW_1});
    menu->addChild(m_findButton);

    m_stopButton = makeButton("Stop", 70.f, menu_selector(PathfinderPopup::onStop));
    m_stopButton->setPosition(CCPoint{COL_MIDDLE, ROW_1});
    menu->addChild(m_stopButton);

    m_playButton = makeButton("Play Macro", 120.f, menu_selector(PathfinderPopup::onPlay));
    m_playButton->setPosition(CCPoint{COL_RIGHT, ROW_1});
    menu->addChild(m_playButton);

    // created by hand so the caption can be swapped while recording
    m_recordSprite = ButtonSprite::create("Record", 120, true, "goldFont.fnt", "GJ_button_01.png", 0.f, 0.6f);
    m_recordButton = CCMenuItemSpriteExtra::create(m_recordSprite, this, menu_selector(PathfinderPopup::onRecord));
    m_recordButton->setSizeMult(1.6f);
    m_recordButton->setPosition(CCPoint{COL_LEFT, ROW_2});
    menu->addChild(m_recordButton);

    m_importButton = makeButton("Import .slc", 120.f, menu_selector(PathfinderPopup::onImport));
    m_importButton->setPosition(CCPoint{COL_MIDDLE, ROW_2});
    menu->addChild(m_importButton);

    m_exportButton = makeButton("Export .slc", 120.f, menu_selector(PathfinderPopup::onExport));
    m_exportButton->setPosition(CCPoint{COL_RIGHT, ROW_2});
    menu->addChild(m_exportButton);

    m_quickExportButton = makeButton("Quick Export", 120.f, menu_selector(PathfinderPopup::onQuickExport));
    m_quickExportButton->setPosition(CCPoint{COL_LEFT, ROW_3});
    menu->addChild(m_quickExportButton);

    m_dumpButton = makeButton("JSON + TXT", 120.f, menu_selector(PathfinderPopup::onDump));
    m_dumpButton->setPosition(CCPoint{COL_MIDDLE, ROW_3});
    menu->addChild(m_dumpButton);

    auto* folderButton = makeButton("Open Folder", 120.f, menu_selector(PathfinderPopup::onFolder));
    folderButton->setPosition(CCPoint{COL_RIGHT, ROW_3});
    menu->addChild(folderButton);

    auto* settingsButton = makeButton("Search Settings", 150.f, menu_selector(PathfinderPopup::onSettings));
    settingsButton->setPosition(CCPoint{120.f, ROW_4});
    menu->addChild(settingsButton);

    auto* aboutButton = makeButton("About", 90.f, menu_selector(PathfinderPopup::onAbout));
    aboutButton->setPosition(CCPoint{280.f, ROW_4});
    menu->addChild(aboutButton);

    refresh();
    return true;
}

void PathfinderPopup::refresh() {
    auto& session = Session::get();
    auto const mode = session.mode();
    Macro const& macro = session.macro();

    if (m_levelLabel) {
        std::string level = macro.levelName.empty() ? "no level open" : macro.levelName;
        if (macro.levelID > 0) level += "  (ID " + std::to_string(macro.levelID) + ")";
        if (macro.platformer) level += "  [platformer]";
        m_levelLabel->setString(level.c_str());
    }

    if (m_macroLabel) {
        m_macroLabel->setString(macro.empty() ? "no macro loaded" : dump::summary(macro).c_str());
    }

    if (m_statusLabel) {
        auto const& stats = session.stats();
        char buffer[512];
        std::snprintf(
            buffer, sizeof(buffer),
            "%s\nbest %.2f%% | %llu frames simulated | %llu segments | %llu deaths | %llu backtracks",
            session.statusText().c_str(), stats.bestProgress * 100.0,
            static_cast<unsigned long long>(stats.simulatedFrames),
            static_cast<unsigned long long>(stats.segmentsCommitted),
            static_cast<unsigned long long>(stats.deathsObserved),
            static_cast<unsigned long long>(stats.backtracks)
        );
        m_statusLabel->setString(buffer);
    }

    if (m_hintLabel) {
        char const* hint = "Find Path restarts the level in practice mode and searches from the beginning.";
        switch (mode) {
            case Session::Mode::Searching: hint = "Searching. The game is being stepped by the mod; you can close this window."; break;
            case Session::Mode::Running: hint = "Playing the macro back through the real engine to verify it."; break;
            case Session::Mode::Recording: hint = "Recording your own inputs. Play the level, then press Stop Recording."; break;
            case Session::Mode::Restarting: hint = "Rewinding the level to its start…"; break;
            default: break;
        }
        m_hintLabel->setString(hint);
    }

    bool const busy = mode != Session::Mode::Idle;
    if (m_findButton) m_findButton->setEnabled(!busy && Session::get().layer() != nullptr);
    if (m_stopButton) m_stopButton->setEnabled(busy);
    if (m_playButton) m_playButton->setEnabled(!busy && !macro.empty() && Session::get().layer() != nullptr);
    if (m_importButton) m_importButton->setEnabled(!busy);
    if (m_exportButton) m_exportButton->setEnabled(!busy && !macro.empty());
    if (m_quickExportButton) m_quickExportButton->setEnabled(!busy && !macro.empty());
    if (m_dumpButton) m_dumpButton->setEnabled(!busy && !macro.empty());

    if (m_recordButton && m_recordSprite) {
        char const* caption = (mode == Session::Mode::Recording) ? "Stop Recording" : "Record";
        m_recordSprite->setString(caption);
        m_recordButton->updateSprite();
        m_recordButton->setEnabled(mode == Session::Mode::Idle || mode == Session::Mode::Recording);
    }
}

void PathfinderPopup::update(float dt) {
    CCNode::update(dt);
    refresh();
}

void PathfinderPopup::onFind(CCObject* sender) {
    Session::get().startSearch();
    refresh();
}

void PathfinderPopup::onStop(CCObject* sender) {
    Session::get().stop();
    refresh();
}

void PathfinderPopup::onPlay(CCObject* sender) {
    Session::get().runMacro();
    refresh();
}

void PathfinderPopup::onRecord(CCObject* sender) {
    auto& session = Session::get();
    if (session.mode() == Session::Mode::Recording) session.stopRecording();
    else session.startRecording();
    refresh();
}

void PathfinderPopup::onImport(CCObject* sender) { Session::get().importMacro(); }

void PathfinderPopup::onExport(CCObject* sender) { Session::get().exportMacro(); }

void PathfinderPopup::onQuickExport(CCObject* sender) { Session::get().exportToSaveDir(); }

void PathfinderPopup::onDump(CCObject* sender) { Session::get().exportDump(); }

void PathfinderPopup::onFolder(CCObject* sender) { Session::get().openSaveFolder(); }

void PathfinderPopup::onSettings(CCObject* sender) {
    if (auto* mod = Mod::get()) geode::openSettingsPopup(mod);
}

void PathfinderPopup::onAbout(CCObject* sender) {
    if (auto* mod = Mod::get()) geode::openInfoPopup(mod);
}

} // namespace pathfinder

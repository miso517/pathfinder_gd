#include "HudLayer.hpp"

#include "../session/Session.hpp"
#include "PathfinderPopup.hpp"

#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;

namespace pathfinder {

HudLayer* HudLayer::create(PlayLayer* layer) {
    auto* ret = new HudLayer();
    if (ret && ret->init(layer)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

HudLayer::~HudLayer() { Session::get().detachHud(this); }

bool HudLayer::init(PlayLayer* layer) {
    if (!CCNode::init()) return false;
    m_layer = layer;

    auto const winSize = CCDirector::get()->getWinSize();
    this->setContentSize(CCSize{230.f, 46.f});
    this->setAnchorPoint(CCPoint{0.f, 1.f});
    // Top left, clear of the pause button.
    this->setPosition(CCPoint{8.f, winSize.height - 46.f});
    this->setID("pathfinder/hud");

    auto* background = CCScale9Sprite::create("square02b_001.png", CCRect{0.f, 0.f, 40.f, 40.f});
    background->setContentSize(CCSize{230.f, 46.f});
    background->setAnchorPoint(CCPoint{0.f, 1.f});
    background->setPosition(CCPointZero);
    background->setColor(ccc3(0, 0, 0));
    background->setOpacity(110);
    this->addChild(background, 0);

    m_statusLabel = CCLabelBMFont::create("pathfinder idle", "chatFont.fnt");
    m_statusLabel->setScale(0.55f);
    m_statusLabel->setAnchorPoint(CCPoint{0.f, 1.f});
    m_statusLabel->setPosition(CCPoint{8.f, -6.f});
    this->addChild(m_statusLabel, 1);

    m_detailLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_detailLabel->setScale(0.45f);
    m_detailLabel->setAnchorPoint(CCPoint{0.f, 1.f});
    m_detailLabel->setPosition(CCPoint{8.f, -20.f});
    m_detailLabel->setColor(ccc3(190, 190, 190));
    this->addChild(m_detailLabel, 1);

    auto* menu = CCMenu::create();
    menu->setContentSize(CCSize{230.f, 46.f});
    menu->setAnchorPoint(CCPoint{0.f, 1.f});
    menu->setPosition(CCPointZero);
    this->addChild(menu, 2);

    m_menuButton = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Menu", 52, true, "goldFont.fnt", "GJ_button_01.png", 0.f, 0.5f), this,
        menu_selector(HudLayer::onMenu)
    );
    m_menuButton->setPosition(CCPoint{172.f, -13.f});
    menu->addChild(m_menuButton);

    m_stopButton = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Stop", 52, true, "goldFont.fnt", "GJ_button_01.png", 0.f, 0.5f), this,
        menu_selector(HudLayer::onStop)
    );
    m_stopButton->setPosition(CCPoint{172.f, -32.f});
    m_stopButton->setVisible(false);
    menu->addChild(m_stopButton);

    Session::get().attachHud(this);
    refresh();
    return true;
}

void HudLayer::refresh() {
    if (!m_statusLabel) return;

    auto& session = Session::get();
    m_statusLabel->setString(session.statusText().c_str());
    m_detailLabel->setString(session.detailText().c_str());

    bool const busy = session.mode() == Session::Mode::Searching || session.mode() == Session::Mode::Running ||
                      session.mode() == Session::Mode::Recording || session.mode() == Session::Mode::Restarting;
    if (m_stopButton) m_stopButton->setVisible(busy);

    // Keep the readout inside the window even on odd resolutions.
    auto const winSize = CCDirector::get()->getWinSize();
    this->setPositionY(winSize.height - 46.f);

    bool const visible = Mod::get() ? Mod::get()->getSettingValue<bool>("show-hud") : true;
    this->setVisible(visible);
}

void HudLayer::setVisibleHud(bool visible) { this->setVisible(visible); }

void HudLayer::onMenu(CCObject* sender) { openPathfinderPopup(); }

void HudLayer::onStop(CCObject* sender) { Session::get().stop(); }

} // namespace pathfinder

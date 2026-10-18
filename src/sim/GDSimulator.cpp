#include "GDSimulator.hpp"

#include <Geode/Bindings.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace geode::prelude;

namespace pathfinder {

GDSimulator::GDSimulator(PlayLayer* layer, double tps) : m_layer(layer) { setTps(tps); }

GDSimulator::~GDSimulator() {
    releaseAll();
    clearSuspend();
    if (m_root) {
        m_root->release();
        m_root = nullptr;
    }
    if (sim::active() == this) sim::active() = nullptr;
}

void GDSimulator::setTps(double tps) {
    if (!(tps > 0.0)) tps = 240.0;
    // Anything above a few thousand ticks per second only melts the CPU.
    if (tps > 4000.0) tps = 4000.0;
    m_tps = tps;
    m_frameTime = static_cast<float>(1.0 / tps);
}

bool GDSimulator::ready() const { return m_layer != nullptr && m_layer->m_player1 != nullptr && m_root != nullptr; }

void GDSimulator::measureLevelBounds() {
    m_startX = m_layer && m_layer->m_player1 ? static_cast<double>(m_layer->m_player1->getPositionX()) : 0.0;

    double endX = 0.0;
    if (m_layer) {
        endX = static_cast<double>(m_layer->m_endPosition.x);
        double const maxObjectX = static_cast<double>(m_layer->m_maxObjectX);
        if (maxObjectX > endX) endX = maxObjectX;
        if (endX <= m_startX + 1.0 && m_layer->m_level) {
            double const length = static_cast<double>(m_layer->m_level->m_levelLength);
            endX = m_startX + (length > 1.0 ? length : 1000.0);
        }
    }
    m_endX = endX > m_startX + 1.0 ? endX : m_startX + 1000.0;
}

bool GDSimulator::captureRoot() {
    if (!m_layer || !m_layer->m_player1) return false;

    CheckpointObject* checkpoint = m_layer->createCheckpoint();
    if (!checkpoint) return false;
    checkpoint->retain();

    if (m_root) m_root->release();
    m_root = checkpoint;
    m_rootFrame = m_frame;

    m_dead = false;
    m_levelFinished = false;
    m_windowFrame = 0;
    m_planCursor = 0;
    m_plan.clear();

    measureLevelBounds();

    // If the game itself thinks objects in this level need their state saved,
    // then the cheap player-only rewind is not enough and the search has to pay
    // for full restores between candidates.
    m_dynamicState = checkpoint->m_vectorSavedObjectStateRef.size() != 0 ||
                     checkpoint->m_vectorActiveSaveObjectState.size() != 0 ||
                     checkpoint->m_vectorSpecialSaveObjectState.size() != 0;

    return true;
}

bool GDSimulator::reset() {
    if (!m_root || !m_layer) return false;

    Entry entry;
    entry.checkpoint = m_root;
    entry.kind = SnapshotKind::Full;
    entry.frame = m_rootFrame;
    entry.inUse = true;

    sim::StepGuard guard;
    applyRestore(entry);
    m_frame = m_rootFrame;
    return true;
}

double GDSimulator::posX() const {
    return (m_layer && m_layer->m_player1) ? static_cast<double>(m_layer->m_player1->getPositionX()) : 0.0;
}

double GDSimulator::posY() const {
    return (m_layer && m_layer->m_player1) ? static_cast<double>(m_layer->m_player1->getPositionY()) : 0.0;
}

double GDSimulator::progress() const {
    double const span = m_endX - m_startX;
    if (span <= 1.0) return 0.0;
    double const p = (posX() - m_startX) / span;
    if (p < 0.0) return 0.0;
    if (p > 1.0) return 1.0;
    return p;
}

bool GDSimulator::alive() const {
    if (m_dead) return false;
    if (m_layer && m_layer->m_player1 && m_layer->m_player1->m_isDead) return false;
    return true;
}

bool GDSimulator::finished() const { return m_levelFinished; }

bool GDSimulator::onGround() const { return m_layer && m_layer->m_player1 && m_layer->m_player1->m_isOnGround; }

bool GDSimulator::dualMode() const { return m_layer && m_layer->m_gameState.m_isDualMode; }

bool GDSimulator::platformer() const { return m_layer && m_layer->m_level && m_layer->m_level->isPlatformer(); }

void GDSimulator::beginWindow(Plan const& plan) {
    m_plan = plan;
    // The stepping loop consumes the plan in order, so it has to be sorted.
    std::stable_sort(m_plan.begin(), m_plan.end(), [](PlanInput const& a, PlanInput const& b) {
        return a.offset < b.offset;
    });
    m_planCursor = 0;
    m_windowFrame = 0;
}

uint32_t GDSimulator::step(uint32_t maxFrames) {
    if (!ready() || maxFrames == 0) return 0;
    if (m_dead || m_levelFinished) return 0;

    // Every update() call below re-enters the Geode hook; the guard tells the
    // hook to just forward to the original implementation.
    sim::StepGuard guard;

    uint32_t stepped = 0;
    for (uint32_t i = 0; i < maxFrames; i++) {
        if (m_dead || m_levelFinished) break;
        if (!m_layer || !m_layer->m_player1) break;

        while (m_planCursor < m_plan.size() && m_plan[m_planCursor].offset <= m_windowFrame) {
            PlanInput const& input = m_plan[m_planCursor++];
            m_layer->handleButton(input.holding, static_cast<int>(input.button), !input.player2);
        }

        m_layer->update(m_frameTime);

        m_windowFrame++;
        m_frame++;
        m_stepsRun++;
        stepped++;

        if (m_layer->m_player1 && m_layer->m_player1->m_isDead) m_dead = true;
    }
    return stepped;
}

void GDSimulator::clearSuspend() {
    if (m_suspendCheckpoint) {
        m_suspendCheckpoint->release();
        m_suspendCheckpoint = nullptr;
    }
    m_suspended = false;
    m_suspendPlan.clear();
    m_suspendCursor = 0;
}

bool GDSimulator::suspendWindow() {
    if (m_suspended) return true;
    if (!m_layer || !m_layer->m_player1) return false;

    sim::StepGuard guard;
    CheckpointObject* checkpoint = m_layer->createCheckpoint();
    if (!checkpoint) return false;
    checkpoint->retain();

    m_suspendCheckpoint = checkpoint;
    m_suspendFrame = m_frame;
    m_suspendWindowFrame = m_windowFrame;
    m_suspendPlan = m_plan;
    m_suspendCursor = m_planCursor;
    m_suspendDead = m_dead;
    m_suspendFinished = m_levelFinished;
    m_suspended = true;
    return true;
}

bool GDSimulator::resumeWindow() {
    if (!m_suspended || !m_suspendCheckpoint || !m_layer) return false;

    Entry entry;
    entry.checkpoint = m_suspendCheckpoint;
    entry.kind = SnapshotKind::Full;
    entry.frame = m_suspendFrame;
    entry.inUse = true;

    {
        sim::StepGuard guard;
        applyRestore(entry);
    }

    // applyRestore clears the window state, so put it back afterwards.
    m_frame = m_suspendFrame;
    m_windowFrame = m_suspendWindowFrame;
    m_plan = m_suspendPlan;
    m_planCursor = m_suspendCursor;
    m_dead = m_suspendDead;
    m_levelFinished = m_suspendFinished;

    if (m_suspendCheckpoint) m_suspendCheckpoint->release();
    m_suspendCheckpoint = nullptr;
    m_suspended = false;
    return true;
}

Snapshot GDSimulator::save(SnapshotKind kind) {
    if (!m_layer || !m_layer->m_player1) return NO_SNAPSHOT;

    sim::StepGuard guard;
    CheckpointObject* checkpoint = m_layer->createCheckpoint();
    if (!checkpoint) return NO_SNAPSHOT;
    checkpoint->retain();

    return store(checkpoint, kind);
}

Snapshot GDSimulator::store(CheckpointObject* checkpoint, SnapshotKind kind) {
    Entry entry;
    entry.checkpoint = checkpoint;
    entry.kind = kind;
    entry.frame = m_frame;
    entry.inUse = true;

    if (!m_freeList.empty()) {
        Snapshot const handle = m_freeList.back();
        m_freeList.pop_back();
        m_entries[static_cast<size_t>(handle) - 1] = entry;
        return handle;
    }
    m_entries.push_back(entry);
    return static_cast<Snapshot>(m_entries.size());
}

GDSimulator::Entry* GDSimulator::find(Snapshot handle) {
    if (handle == NO_SNAPSHOT || handle > m_entries.size()) return nullptr;
    Entry& entry = m_entries[static_cast<size_t>(handle) - 1];
    return entry.inUse ? &entry : nullptr;
}

void GDSimulator::applyRestore(Entry& entry) {
    if (!m_layer || !entry.checkpoint) return;

    if (entry.kind == SnapshotKind::Full) {
        m_layer->loadFromCheckpoint(entry.checkpoint);
    } else {
        // Cheap path: only the icons are rewound. Only legal for levels without
        // player mutable state, which is decided in hasDynamicState().
        if (m_layer->m_player1 && entry.checkpoint->m_player1Checkpoint) {
            m_layer->m_player1->loadFromCheckpoint(entry.checkpoint->m_player1Checkpoint);
        }
        if (dualMode() && m_layer->m_player2 && entry.checkpoint->m_player2Checkpoint) {
            m_layer->m_player2->loadFromCheckpoint(entry.checkpoint->m_player2Checkpoint);
        }
    }

    m_frame = entry.frame;
    m_windowFrame = 0;
    m_planCursor = 0;
    m_dead = false;
    m_levelFinished = false;
    m_restoresRun++;
}

void GDSimulator::restore(Snapshot handle) {
    Entry* entry = find(handle);
    if (!entry) return;

    sim::StepGuard guard;
    applyRestore(*entry);
}

void GDSimulator::release(Snapshot handle) {
    Entry* entry = find(handle);
    if (!entry) return;

    if (entry->checkpoint && entry->checkpoint != m_root) entry->checkpoint->release();
    entry->checkpoint = nullptr;
    entry->inUse = false;
    m_freeList.push_back(handle);
}

void GDSimulator::releaseAll() {
    clearSuspend();
    for (size_t i = 0; i < m_entries.size(); i++) {
        Entry& entry = m_entries[i];
        if (!entry.inUse) continue;
        if (entry.checkpoint && entry.checkpoint != m_root) entry.checkpoint->release();
        entry.checkpoint = nullptr;
        entry.inUse = false;
        m_freeList.push_back(static_cast<Snapshot>(i + 1));
    }
}

uint64_t GDSimulator::checkpointsHeld() const {
    uint64_t held = 0;
    for (auto const& entry : m_entries) {
        if (entry.inUse) held++;
    }
    if (m_root) held++;
    if (m_suspendCheckpoint) held++;
    return held;
}

bool GDSimulator::suppressEffects() const { return sim::stepping(); }

void GDSimulator::notifyDeath(PlayerObject* player) {
    if (!m_layer) return;
    // In dual mode losing either icon ends the run.
    if (player && player != m_layer->m_player1 && player != m_layer->m_player2) return;
    m_dead = true;
}

void GDSimulator::notifyFinish() { m_levelFinished = true; }

std::string GDSimulator::describe() const {
    if (!m_layer || !m_layer->m_player1) return "no level";

    PlayerObject* player = m_layer->m_player1;
    char const* mode = "cube";
    if (player->m_isShip) mode = "ship";
    else if (player->m_isBird) mode = "ufo";
    else if (player->m_isBall) mode = "ball";
    else if (player->m_isDart) mode = "wave";
    else if (player->m_isRobot) mode = "robot";
    else if (player->m_isSpider) mode = "spider";
    else if (player->m_isSwing) mode = "swing";

    char buffer[128];
    std::snprintf(
        buffer, sizeof(buffer), "%s%s | x %.0f y %.0f | frame %llu", mode, dualMode() ? "+dual" : "", posX(), posY(),
        static_cast<unsigned long long>(m_frame)
    );
    return std::string(buffer);
}

} // namespace pathfinder

#include "Plans.hpp"

#include <algorithm>

namespace pathfinder {

namespace {

    void addPress(std::vector<Plan>& plans, uint32_t offset, Button button, uint32_t horizon, uint32_t hold,
                  bool player2) {
        Plan plan;
        PlanInput press;
        press.offset = offset;
        press.button = button;
        press.holding = true;
        press.player2 = player2;
        plan.push_back(press);

        if (hold > 0 && offset + hold < horizon) {
            PlanInput release = press;
            release.offset = offset + hold;
            release.holding = false;
            plan.push_back(release);
        }
        plans.push_back(std::move(plan));
    }

    void addRelease(std::vector<Plan>& plans, uint32_t offset, Button button, bool player2) {
        Plan plan;
        PlanInput release;
        release.offset = offset;
        release.button = button;
        release.holding = false;
        release.player2 = player2;
        plan.push_back(release);
        plans.push_back(std::move(plan));
    }

    /// Evenly thins `plans` down to at most `maxPlans` entries while keeping the
    /// first one (the "coast" plan) and spreading the survivors over every
    /// category, so a small budget still samples the whole horizon.
    void thin(std::vector<Plan>& plans, size_t maxPlans) {
        if (maxPlans == 0 || plans.size() <= maxPlans) return;

        std::vector<Plan> kept;
        kept.reserve(maxPlans);
        kept.push_back(std::move(plans.front()));

        size_t const remaining = maxPlans - 1;
        size_t const pool = plans.size() - 1;
        for (size_t i = 0; i < remaining; i++) {
            size_t const idx = 1 + (i * pool) / remaining;
            if (idx < plans.size()) kept.push_back(std::move(plans[idx]));
        }
        plans.swap(kept);

        // Sampling can produce duplicates (e.g. when several indices collapse);
        // dropping them keeps the budget honest.
        plans.erase(
            std::unique(
                plans.begin(), plans.end(),
                [](Plan const& a, Plan const& b) {
                    if (a.size() != b.size()) return false;
                    for (size_t i = 0; i < a.size(); i++) {
                        if (a[i].offset != b[i].offset || a[i].button != b[i].button ||
                            a[i].holding != b[i].holding || a[i].player2 != b[i].player2) {
                            return false;
                        }
                    }
                    return true;
                }
            ),
            plans.end()
        );
    }

} // namespace

std::vector<Plan> generatePlans(SearchParams const& params, uint32_t horizon, PlanContext const& context) {
    std::vector<Plan> plans;
    if (horizon == 0) return plans;

    // 0. Change nothing: coasting is always a legal option and is what the
    //    search should pick whenever the level does not demand input.
    plans.push_back(Plan{});

    uint32_t const hold = params.holdFrames == 0 ? 1 : params.holdFrames;
    std::vector<uint32_t> holds = {hold};
    if (hold != 1) holds.push_back(1);
    if (hold * 2 <= horizon) holds.push_back(hold * 2);
    holds.push_back(horizon); // pressed and never released inside the window

    // 1./3. Jump presses at every offset, for each hold length.
    for (uint32_t h : holds) {
        for (uint32_t offset = 0; offset < horizon; offset++) {
            addPress(plans, offset, Button::Jump, horizon, h, false);
            if (params.dualMode) addPress(plans, offset, Button::Jump, horizon, h, true);
        }
    }

    // 2. Letting go of a button that is still held from the previous window.
    if (context.holdJump) {
        for (uint32_t offset = 0; offset < horizon; offset++) addRelease(plans, offset, Button::Jump, false);
        if (params.dualMode) {
            for (uint32_t offset = 0; offset < horizon; offset++) addRelease(plans, offset, Button::Jump, true);
        }
    }

    // 4. Directional input, only meaningful in platformer mode.
    if (params.allowLeftRight) {
        for (Button button : {Button::Left, Button::Right}) {
            for (uint32_t h : holds) {
                for (uint32_t offset = 0; offset < horizon; offset++) addPress(plans, offset, button, horizon, h, false);
            }
            bool const held = (button == Button::Left) ? context.holdLeft : context.holdRight;
            if (held) {
                for (uint32_t offset = 0; offset < horizon; offset++) addRelease(plans, offset, button, false);
            }
        }
    }

    thin(plans, params.maxPlansPerSegment);
    return plans;
}

void planToEvents(Plan const& plan, uint64_t baseFrame, bool player2Mirror, std::vector<MacroEvent>& events) {
    for (auto const& input : plan) {
        MacroEvent e;
        e.frame = baseFrame + input.offset;
        e.type = eventOf(input.button);
        e.holding = input.holding;
        e.player2 = input.player2;
        events.push_back(e);

        // In dual mode both icons usually have to be driven together; mirroring
        // keeps the plan list small instead of duplicating every candidate.
        if (player2Mirror && !input.player2 && input.button == Button::Jump) {
            MacroEvent mirrored = e;
            mirrored.player2 = true;
            events.push_back(mirrored);
        }
    }
}

void planCarryOver(Plan const& plan, uint32_t horizon, PlanContext& context) {
    for (auto const& input : plan) {
        if (input.offset >= horizon) continue;
        if (input.player2) continue; // carried state only tracks player 1
        switch (input.button) {
            case Button::Jump: context.holdJump = input.holding; break;
            case Button::Left: context.holdLeft = input.holding; break;
            case Button::Right: context.holdRight = input.holding; break;
        }
    }
}

} // namespace pathfinder

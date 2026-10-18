#include "SimSignals.hpp"

namespace pathfinder::sim {

bool& stepping() {
    static bool value = false;
    return value;
}

GDSimulator*& active() {
    static GDSimulator* value = nullptr;
    return value;
}

} // namespace pathfinder::sim

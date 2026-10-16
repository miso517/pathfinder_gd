#include "Dump.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace pathfinder::dump {

namespace {

    char const* eventTypeName(EventType type) {
        switch (type) {
            case EventType::Jump: return "jump";
            case EventType::Left: return "left";
            case EventType::Right: return "right";
            case EventType::Restart: return "restart";
            case EventType::RestartFull: return "restart-full";
            case EventType::Death: return "death";
            case EventType::TPS: return "tps";
            case EventType::Bugpoint: return "bugpoint";
            default: return "reserved";
        }
    }

    bool eventTypeFromName(std::string const& name, EventType& out) {
        if (name == "jump") out = EventType::Jump;
        else if (name == "left") out = EventType::Left;
        else if (name == "right") out = EventType::Right;
        else if (name == "restart") out = EventType::Restart;
        else if (name == "restart-full") out = EventType::RestartFull;
        else if (name == "death") out = EventType::Death;
        else if (name == "tps") out = EventType::TPS;
        else if (name == "bugpoint") out = EventType::Bugpoint;
        else return false;
        return true;
    }

    /// Finds the value that follows `"key"` inside `[begin, end)`.
    std::string findValue(std::string const& text, std::string const& key, size_t begin, size_t end) {
        std::string const needle = "\"" + key + "\"";
        size_t const at = text.find(needle, begin);
        if (at == std::string::npos || at >= end) return std::string();

        size_t colon = text.find(':', at + needle.size());
        if (colon == std::string::npos || colon >= end) return std::string();

        size_t i = colon + 1;
        while (i < end && std::isspace(static_cast<unsigned char>(text[i]))) i++;
        if (i >= end) return std::string();

        if (text[i] == '"') {
            size_t close = i + 1;
            std::string value;
            while (close < end) {
                if (text[close] == '\\' && close + 1 < end) {
                    char const next = text[close + 1];
                    switch (next) {
                        case 'n': value += '\n'; break;
                        case 't': value += '\t'; break;
                        case 'r': break;
                        default: value += next; break;
                    }
                    close += 2;
                    continue;
                }
                if (text[close] == '"') break;
                value += text[close++];
            }
            return value;
        }

        size_t close = i;
        while (close < end && text[close] != ',' && text[close] != '}' && text[close] != ']' &&
               !std::isspace(static_cast<unsigned char>(text[close]))) {
            close++;
        }
        return text.substr(i, close - i);
    }

    double toDouble(std::string const& text, double fallback) {
        if (text.empty()) return fallback;
        return std::strtod(text.c_str(), nullptr);
    }

    long long toInt(std::string const& text, long long fallback) {
        if (text.empty()) return fallback;
        return std::strtoll(text.c_str(), nullptr, 10);
    }

    bool toBool(std::string const& text, bool fallback) {
        if (text.empty()) return fallback;
        if (text == "true") return true;
        if (text == "false") return false;
        return toInt(text, fallback ? 1 : 0) != 0;
    }

} // namespace

std::string escapeJson(std::string const& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string toJson(Macro const& macro) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"format\": \"pathfinder-macro\",\n";
    out << "  \"version\": 1,\n";
    out << "  \"tps\": " << macro.tps << ",\n";
    out << "  \"seed\": " << macro.seed << ",\n";
    out << "  \"frames\": " << macro.frameCount << ",\n";
    out << "  \"level\": \"" << escapeJson(macro.levelName) << "\",\n";
    out << "  \"levelId\": " << macro.levelID << ",\n";
    out << "  \"platformer\": " << (macro.platformer ? "true" : "false") << ",\n";
    out << "  \"source\": \"" << escapeJson(macro.source) << "\",\n";
    out << "  \"notes\": \"" << escapeJson(macro.notes) << "\",\n";
    out << "  \"events\": [\n";
    for (size_t i = 0; i < macro.events.size(); i++) {
        MacroEvent const& e = macro.events[i];
        out << "    { \"frame\": " << e.frame << ", \"type\": \"" << eventTypeName(e.type) << "\"";
        if (isPlayerEvent(e.type)) {
            out << ", \"holding\": " << (e.holding ? "true" : "false");
            out << ", \"player2\": " << (e.player2 ? "true" : "false");
        } else if (e.type == EventType::TPS) {
            out << ", \"tps\": " << e.tps;
        } else {
            out << ", \"seed\": " << e.seed;
        }
        out << " }" << (i + 1 == macro.events.size() ? "" : ",") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

std::string toText(Macro const& macro) {
    std::ostringstream out;
    out << summary(macro) << "\n";
    out << "frame\ttype\tholding\tplayer\n";
    for (auto const& e : macro.events) {
        out << e.frame << '\t' << eventTypeName(e.type) << '\t';
        if (isPlayerEvent(e.type)) {
            out << (e.holding ? "press" : "release") << '\t' << (e.player2 ? 2 : 1) << '\n';
        } else if (e.type == EventType::TPS) {
            out << e.tps << "\t-\n";
        } else {
            out << e.seed << "\t-\n";
        }
    }
    return out.str();
}

std::string summary(Macro const& macro) {
    size_t presses = 0;
    for (auto const& e : macro.events) {
        if (isPlayerEvent(e.type) && e.holding) presses++;
    }
    char buffer[512];
    std::snprintf(
        buffer, sizeof(buffer),
        "%s | %.3g tps | %zu events | %zu presses | %llu frames (%.2fs)",
        macro.levelName.empty() ? "untitled macro" : macro.levelName.c_str(), macro.tps, macro.events.size(), presses,
        static_cast<unsigned long long>(macro.frameCount), macro.tps > 0.0 ? macro.frameCount / macro.tps : 0.0
    );
    return std::string(buffer);
}

bool fromJson(std::string const& json, Macro& out, std::string& error) {
    if (json.find("\"pathfinder-macro\"") == std::string::npos) {
        error = "not a pathfinder macro dump";
        return false;
    }

    Macro macro;
    macro.tps = toDouble(findValue(json, "tps", 0, json.size()), 240.0);
    if (!(macro.tps > 0.0)) macro.tps = 240.0;
    macro.seed = static_cast<uint64_t>(toInt(findValue(json, "seed", 0, json.size()), 0));
    macro.frameCount = static_cast<uint64_t>(toInt(findValue(json, "frames", 0, json.size()), 0));
    macro.levelName = findValue(json, "level", 0, json.size());
    macro.levelID = static_cast<int>(toInt(findValue(json, "levelId", 0, json.size()), 0));
    macro.platformer = toBool(findValue(json, "platformer", 0, json.size()), false);
    macro.source = findValue(json, "source", 0, json.size());
    macro.notes = findValue(json, "notes", 0, json.size());

    size_t const eventsKey = json.find("\"events\"");
    if (eventsKey == std::string::npos) {
        error = "the dump has no event list";
        return false;
    }
    size_t const arrayBegin = json.find('[', eventsKey);
    size_t const arrayEnd = json.rfind(']');
    if (arrayBegin == std::string::npos || arrayEnd == std::string::npos || arrayEnd < arrayBegin) {
        error = "the event list is malformed";
        return false;
    }

    size_t cursor = arrayBegin;
    while (true) {
        size_t const open = json.find('{', cursor);
        if (open == std::string::npos || open >= arrayEnd) break;
        size_t const close = json.find('}', open);
        if (close == std::string::npos || close > arrayEnd) break;

        MacroEvent event;
        event.frame = static_cast<uint64_t>(toInt(findValue(json, "frame", open, close), 0));
        std::string const typeName = findValue(json, "type", open, close);
        if (!eventTypeFromName(typeName, event.type)) {
            error = "unknown event type '" + typeName + "'";
            return false;
        }
        event.holding = toBool(findValue(json, "holding", open, close), false);
        event.player2 = toBool(findValue(json, "player2", open, close), false);
        event.tps = toDouble(findValue(json, "tps", open, close), macro.tps);
        event.seed = static_cast<uint64_t>(toInt(findValue(json, "seed", open, close), 0));
        macro.events.push_back(event);

        cursor = close + 1;
    }

    macro.normalize();
    if (macro.frameCount == 0) macro.frameCount = macro.lastFrame();
    out = std::move(macro);
    return true;
}

} // namespace pathfinder::dump

#pragma once

// Human readable and JSON dumps of a macro.
//
// Useful for debugging a search result, for diffing two runs and for sharing a
// path with someone who does not have Silicate installed. Hand rolled JSON so
// that this file stays free of any game or Geode dependency.

#include "../core/Types.hpp"

#include <string>

namespace pathfinder::dump {

/// Pretty printed JSON, e.g. `{ "tps": 240, "events": [ { "frame": 12, ... } ] }`.
std::string toJson(Macro const& macro);

/// One line per event: `frame  button  press/release  player`.
std::string toText(Macro const& macro);

/// Short one line summary used in the UI and in notifications.
std::string summary(Macro const& macro);

/// Parses what `toJson` wrote. Used by the tests and by "import JSON".
bool fromJson(std::string const& json, Macro& out, std::string& error);

std::string escapeJson(std::string const& text);

} // namespace pathfinder::dump

#pragma once

// Format agnostic entry point for reading and writing .slc macros.

#include "../core/Types.hpp"
#include "SlcV2.hpp"
#include "SlcV3.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pathfinder::slc {

enum class Format {
    V3, ///< Current Silicate format, magic `SLC3RPLY`.
    V2, ///< Legacy Silicate format, magic `SILL`.
};

/// Detects the container format from the magic bytes. Returns false when the
/// buffer is neither.
bool sniffFormat(std::vector<uint8_t> const& bytes, Format& formatOut);

/// Encodes a macro. Defaults to v3, which is what Silicate reads today.
bool encode(Macro const& macro, Format format, std::vector<uint8_t>& out, std::string& error);

/// Decodes either container format.
bool decode(std::vector<uint8_t> const& bytes, Macro& out, std::string& error);

bool saveToFile(std::filesystem::path const& path, Macro const& macro, Format format, std::string& error);
bool loadFromFile(std::filesystem::path const& path, Macro& out, std::string& error);

/// Suggested file name for a macro, e.g. `bloodbath_1.0.slc`.
std::string defaultFileName(Macro const& macro, Format format);

} // namespace pathfinder::slc

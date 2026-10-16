#pragma once

// .slc (Silicate replay) version 3 codec.
//
// Container layout (all little endian):
//
//   "SLC3RPLY"            8 bytes   magic
//   uint16 metaSize       2 bytes   always 64
//   Metadata             64 bytes   double tps; uint64 seed; uint32 version;
//                                   uint32 build; uint32 randomnessAlgorithm;
//                                   36 bytes padding
//   Atom*                 ...       [uint32 id][uint64 size][payload]
//   uint8 footer          1 byte    0xCC
//
// The only atom we emit is the ActionAtom (id 1) whose payload is
//   uint64 actionCount
//   Section*
//
// A section starts with a uint16 header whose top two bits select the kind:
//
//   Input   00 SS CCCC 00000000   SS = log2(bytes per entry), CCCC = log2(entry count)
//           followed by (1 << CCCC) entries of (1 << SS) bytes each.
//           entry = (delta << 4) | (button << 2) | (player2 << 1) | holding
//
//   Repeat  01 SS CCCC RRRRR 000  the Input block is repeated (1 << RRRRR) times
//
//   Special 10 TTTT SS 00000000   TTTT = special type, SS = log2(bytes of delta)
//           followed by the delta and then the payload
//           (uint64 seed for Restart/RestartFull/Death, double tps for TPS,
//            nothing for Bugpoint)
//
// Deltas are relative to the frame of the previous action in the stream.

#include "../core/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pathfinder::slc {

/// Byte magic of a v3 container.
inline constexpr char V3_MAGIC[8] = {'S', 'L', 'C', '3', 'R', 'P', 'L', 'Y'};
inline constexpr uint8_t V3_FOOTER = 0xCC;
inline constexpr uint16_t V3_METADATA_SIZE = 64;

enum class AtomId : uint32_t {
    Null = 0,
    Action = 1,
    Marker = 2,
};

enum class SectionId : uint8_t {
    Input = 0,
    Repeat = 1,
    Special = 2,
};

enum class SpecialType : uint8_t {
    Restart = 0,
    RestartFull = 1,
    Death = 2,
    TPS = 3,
    Bugpoint = 4,
};

struct V3Header {
    double tps = 240.0;
    uint64_t seed = 0;
    uint32_t version = 2;
    uint32_t build = 0;
    uint32_t randomnessAlgorithm = 0;
};

/// Number of bytes needed to store a delta, expressed as log2 (0 => 1 byte).
/// `player` selects the 4 bit shift used by input sections, specials reserve 8
/// bits for their own header data.
uint8_t deltaSizeClass(uint64_t delta, bool player);

/// Encodes a macro into a v3 container.
///
/// The event stream is emitted as Input sections that each hold a power of two
/// number of entries (as required by the format) plus one Special section per
/// non-player event. Run length encoding is not performed: the result is a
/// little larger than Silicate's own output but is read back identically.
bool encodeV3(Macro const& macro, V3Header const& header, std::vector<uint8_t>& out, std::string& error);

/// Decodes a v3 container. `headerOut` may be null.
bool decodeV3(std::vector<uint8_t> const& bytes, Macro& macroOut, V3Header* headerOut, std::string& error);

} // namespace pathfinder::slc

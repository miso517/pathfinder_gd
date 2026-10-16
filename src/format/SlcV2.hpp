#pragma once

// .slc version 2 codec (the older "SILL" container).
//
//   "SILL"              4 bytes
//   double tps          8 bytes
//   uint64 metaSize     8 bytes   0 for the metadata-less variant we write
//   [meta]              metaSize bytes
//   uint64 inputCount   8 bytes
//   uint64 blobCount    8 bytes
//   blobCount * {uint64 byteSize, uint64 start, uint64 length}
//   blob payloads       each input stored as the low `byteSize` bytes of
//                       (delta << 5) | (type << 2) | (player2 << 1) | holding,
//                       and a TPS input is followed by its double value
//   "EOM"               3 bytes

#include "../core/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pathfinder::slc {

inline constexpr char V2_MAGIC[4] = {'S', 'I', 'L', 'L'};
inline constexpr char V2_FOOTER[3] = {'E', 'O', 'M'};

bool encodeV2(Macro const& macro, std::vector<uint8_t>& out, std::string& error);
bool decodeV2(std::vector<uint8_t> const& bytes, Macro& macroOut, std::string& error);

} // namespace pathfinder::slc

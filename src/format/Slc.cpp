#include "Slc.hpp"

#include <cstring>
#include <fstream>

namespace pathfinder::slc {

bool sniffFormat(std::vector<uint8_t> const& bytes, Format& formatOut) {
    if (bytes.size() >= 8 && std::memcmp(bytes.data(), V3_MAGIC, 8) == 0) {
        formatOut = Format::V3;
        return true;
    }
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), V2_MAGIC, 4) == 0) {
        formatOut = Format::V2;
        return true;
    }
    return false;
}

bool encode(Macro const& macro, Format format, std::vector<uint8_t>& out, std::string& error) {
    switch (format) {
        case Format::V3: {
            V3Header header;
            header.tps = macro.tps > 0.0 ? macro.tps : 240.0;
            header.seed = macro.seed;
            header.version = 2;
            header.build = 0;
            header.randomnessAlgorithm = 0;
            return encodeV3(macro, header, out, error);
        }
        case Format::V2: return encodeV2(macro, out, error);
    }
    error = "unknown .slc format";
    return false;
}

bool decode(std::vector<uint8_t> const& bytes, Macro& out, std::string& error) {
    Format format = Format::V3;
    if (!sniffFormat(bytes, format)) {
        error = "not an .slc file (expected the SLC3RPLY or SILL magic)";
        return false;
    }
    switch (format) {
        case Format::V3: return decodeV3(bytes, out, nullptr, error);
        case Format::V2: return decodeV2(bytes, out, error);
    }
    error = "unknown .slc format";
    return false;
}

bool saveToFile(std::filesystem::path const& path, Macro const& macro, Format format, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!encode(macro, format, bytes, error)) return false;

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "could not open '" + path.string() + "' for writing";
        return false;
    }
    file.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        error = "failed while writing '" + path.string() + "'";
        return false;
    }
    return true;
}

bool loadFromFile(std::filesystem::path const& path, Macro& out, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "could not open '" + path.string() + "'";
        return false;
    }
    auto const size = file.tellg();
    if (size < 0) {
        error = "could not determine the size of '" + path.string() + "'";
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        error = "failed while reading '" + path.string() + "'";
        return false;
    }
    return decode(bytes, out, error);
}

std::string defaultFileName(Macro const& macro, Format format) {
    std::string base;
    for (char c : macro.levelName) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.') {
            base += c;
        } else if (c == ' ') {
            base += '_';
        }
    }
    while (base.size() > 48) base.pop_back();
    if (base.empty()) base = "macro";

    std::string name = base + "_pathfinder";
    if (macro.frameCount > 0) name += "_" + std::to_string(macro.frameCount) + "f";
    name += (format == Format::V3) ? ".slc" : "_v2.slc";
    return name;
}

} // namespace pathfinder::slc

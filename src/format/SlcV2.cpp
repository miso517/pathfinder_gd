#include "SlcV2.hpp"

#include "../core/ByteIO.hpp"

#include <algorithm>
#include <cstring>

namespace pathfinder::slc {

namespace {

    // v2 has no Bugpoint event; everything else maps 1:1 onto EventType.
    constexpr uint64_t BLOB_META_SIZE = 24; // three uint64s

    struct Input {
        uint64_t state = 0;
        double tps = 0.0;
        uint8_t type = 0;
        uint64_t frame = 0;
        bool player2 = false;
        bool holding = false;
    };

    struct Blob {
        uint64_t byteSize = 1;
        uint64_t start = 0;
        uint64_t length = 0;
    };

    uint8_t requiredBytes(Input const& in) {
        if (in.type == static_cast<uint8_t>(EventType::TPS)) return 8;
        if (in.state < 0x100ull) return 1;
        if (in.state < 0x10000ull) return 2;
        if (in.state < 0x100000000ull) return 4;
        return 8;
    }

    uint64_t maskForBytes(uint64_t byteCount) {
        if (byteCount >= 8) return ~0ull;
        return (1ull << (byteCount * 8ull)) - 1ull;
    }

} // namespace

bool encodeV2(Macro const& macro, std::vector<uint8_t>& out, std::string& error) {
    std::vector<MacroEvent> events = macro.events;
    std::stable_sort(events.begin(), events.end(), eventOrderLess);

    std::vector<Input> inputs;
    inputs.reserve(events.size());

    uint64_t prevFrame = 0;
    for (auto const& e : events) {
        if (e.type == EventType::Bugpoint || e.type == EventType::Reserved) {
            // Not representable in v2; dropping them keeps the rest of the
            // macro playable.
            continue;
        }
        if (e.frame < prevFrame) {
            error = "event stream is not sorted by frame";
            return false;
        }

        Input in;
        uint64_t const delta = e.frame - prevFrame;
        prevFrame = e.frame;
        in.frame = e.frame;
        in.type = static_cast<uint8_t>(e.type);
        in.player2 = e.player2;
        in.holding = e.holding;
        in.tps = e.tps;
        in.state = (delta << 5) | (static_cast<uint64_t>(in.type) << 2) |
                   (static_cast<uint64_t>(e.player2 ? 1 : 0) << 1) | static_cast<uint64_t>(e.holding ? 1 : 0);
        inputs.push_back(in);
    }

    // ---- blob layout
    std::vector<Blob> blobs;
    for (size_t i = 0; i < inputs.size(); i++) {
        uint8_t const size = requiredBytes(inputs[i]);
        if (blobs.empty() || blobs.back().byteSize != size) {
            blobs.push_back(Blob{size, i, 1});
        } else {
            blobs.back().length++;
        }
    }

    // Absorb runs that are cheaper to widen than to describe, mirroring what
    // Silicate does: a blob header costs 24 bytes, so a one input blob only pays
    // off when widening the previous blob would cost more than that.
    for (size_t i = blobs.size(); i-- > 1;) {
        uint64_t const cost = blobs[i].byteSize * blobs[i].length;
        if (cost >= BLOB_META_SIZE && blobs[i].byteSize != blobs[i - 1].byteSize) continue;

        uint64_t const newSize = std::max(blobs[i].byteSize, blobs[i - 1].byteSize);
        uint64_t const extra = (newSize - blobs[i - 1].byteSize) * blobs[i - 1].length;
        if (extra >= BLOB_META_SIZE && cost >= BLOB_META_SIZE) continue;

        blobs[i - 1].byteSize = newSize;
        blobs[i - 1].length += blobs[i].length;
        blobs[i].length = 0;
    }

    size_t liveBlobs = 0;
    for (auto const& b : blobs) {
        if (b.length > 0) liveBlobs++;
    }

    // ---- container
    ByteWriter w;
    w.raw(V2_MAGIC, sizeof(V2_MAGIC));
    w.f64(macro.tps > 0.0 ? macro.tps : 240.0);
    w.u64(0); // no metadata block
    w.u64(static_cast<uint64_t>(inputs.size()));
    w.u64(static_cast<uint64_t>(liveBlobs));

    for (auto const& b : blobs) {
        if (b.length == 0) continue;
        w.u64(b.byteSize);
        w.u64(b.start);
        w.u64(b.length);
    }

    for (auto const& b : blobs) {
        if (b.length == 0) continue;
        uint64_t const mask = maskForBytes(b.byteSize);
        for (uint64_t i = b.start; i < b.start + b.length; i++) {
            w.uintBytes(inputs[static_cast<size_t>(i)].state & mask, static_cast<uint32_t>(b.byteSize));
            if (inputs[static_cast<size_t>(i)].type == static_cast<uint8_t>(EventType::TPS)) {
                w.f64(inputs[static_cast<size_t>(i)].tps);
            }
        }
    }

    w.raw(V2_FOOTER, sizeof(V2_FOOTER));

    out = w.take();
    return true;
}

bool decodeV2(std::vector<uint8_t> const& bytes, Macro& macroOut, std::string& error) {
    ByteReader in(bytes);

    uint8_t magic[4] = {0};
    for (int i = 0; i < 4; i++) {
        auto b = in.u8();
        if (!b) {
            error = "file is too short to be an .slc v2 replay";
            return false;
        }
        magic[i] = *b;
    }
    if (std::memcmp(magic, V2_MAGIC, 4) != 0) {
        error = "not an .slc v2 replay (bad magic)";
        return false;
    }

    Macro macro;
    macro.source = "import";
    if (auto tps = in.f64()) macro.tps = *tps > 0.0 ? *tps : 240.0;
    auto metaSize = in.u64();
    if (!metaSize) {
        error = "truncated v2 header";
        return false;
    }
    if (*metaSize > 0 && !in.skip(static_cast<size_t>(*metaSize))) {
        error = "truncated v2 metadata block";
        return false;
    }

    auto inputCount = in.u64();
    auto blobCount = in.u64();
    if (!inputCount || !blobCount) {
        error = "truncated v2 header";
        return false;
    }
    if (*inputCount > 500'000'000ull || *blobCount > 500'000'000ull) {
        error = "v2 replay claims an implausible size";
        return false;
    }

    std::vector<Blob> blobs;
    blobs.reserve(static_cast<size_t>(*blobCount));
    for (uint64_t i = 0; i < *blobCount; i++) {
        auto byteSize = in.u64();
        auto start = in.u64();
        auto length = in.u64();
        if (!byteSize || !start || !length) {
            error = "truncated v2 blob table";
            return false;
        }
        blobs.push_back(Blob{*byteSize, *start, *length});
    }

    std::vector<Input> inputs(static_cast<size_t>(*inputCount));
    uint64_t frame = 0;
    for (auto const& b : blobs) {
        if (b.start + b.length > *inputCount) {
            error = "v2 blob points outside the input array";
            return false;
        }
        for (uint64_t i = b.start; i < b.start + b.length; i++) {
            auto state = in.uintBytes(static_cast<uint32_t>(b.byteSize));
            if (!state) {
                error = "unexpected end of file inside a v2 blob";
                return false;
            }
            Input& slot = inputs[static_cast<size_t>(i)];
            slot.state = *state;
            uint64_t const delta = *state >> 5;
            frame += delta;
            slot.frame = frame;
            slot.player2 = ((*state >> 1) & 1) != 0;
            slot.holding = (*state & 1) != 0;
            slot.type = static_cast<uint8_t>((*state >> 2) & 0b111);

            if (slot.type == static_cast<uint8_t>(EventType::TPS)) {
                auto tps = in.f64();
                if (!tps) {
                    error = "unexpected end of file while reading a v2 tps value";
                    return false;
                }
                slot.tps = *tps;
            }
        }
    }

    for (auto const& slot : inputs) {
        if (slot.type == 0 || slot.type > static_cast<uint8_t>(EventType::TPS)) continue; // Skip / unknown
        MacroEvent e;
        e.frame = slot.frame;
        e.type = static_cast<EventType>(slot.type);
        e.holding = slot.holding;
        e.player2 = slot.player2;
        e.tps = slot.tps > 0.0 ? slot.tps : 240.0;
        macro.events.push_back(e);
        if (e.type == EventType::TPS && e.tps > 0.0) macro.tps = e.tps;
    }

    macro.normalize();
    macro.frameCount = frame;

    if (in.remaining() >= 3) {
        uint8_t footer[3] = {0};
        for (int i = 0; i < 3; i++) footer[i] = *in.u8();
        if (std::memcmp(footer, V2_FOOTER, 3) != 0) {
            error = "missing .slc v2 footer";
            return false;
        }
    }

    macroOut = std::move(macro);
    return true;
}

} // namespace pathfinder::slc

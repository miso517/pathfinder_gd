#include "SlcV3.hpp"

#include "../core/ByteIO.hpp"

#include <algorithm>

namespace pathfinder::slc {

uint8_t deltaSizeClass(uint64_t delta, bool player) {
    // Player entries keep 4 bits for button/player2/holding, special sections
    // keep 8 bits for their own header data.
    uint64_t const shift = player ? 4 : 8;
    uint64_t const oneByte = 1ull << shift;
    uint64_t const twoBytes = 1ull << (shift + 8);
    uint64_t const fourBytes = 1ull << (shift + 24);

    if (delta < oneByte) return 0;
    if (delta < twoBytes) return 1;
    if (delta < fourBytes) return 2;
    return 3;
}

namespace {

    uint64_t maskForBytes(uint32_t byteCount) {
        if (byteCount >= 8) return ~0ull;
        return (1ull << (byteCount * 8ull)) - 1ull;
    }

    SpecialType specialTypeOf(EventType type) {
        switch (type) {
            case EventType::Restart: return SpecialType::Restart;
            case EventType::RestartFull: return SpecialType::RestartFull;
            case EventType::Death: return SpecialType::Death;
            case EventType::TPS: return SpecialType::TPS;
            default: return SpecialType::Bugpoint;
        }
    }

    EventType eventTypeOf(SpecialType type) {
        switch (type) {
            case SpecialType::Restart: return EventType::Restart;
            case SpecialType::RestartFull: return EventType::RestartFull;
            case SpecialType::Death: return EventType::Death;
            case SpecialType::TPS: return EventType::TPS;
            default: return EventType::Bugpoint;
        }
    }

    // ---------------------------------------------------------------- encoding

    void writeInputSection(ByteWriter& out, std::vector<MacroEvent> const& events, std::vector<uint64_t> const& deltas,
                           size_t begin, size_t count) {
        uint8_t sizeClass = 0;
        for (size_t i = begin; i < begin + count; i++) {
            // Every entry of a section is stored with the same width, so the
            // section has to be wide enough for the largest delta it contains.
            sizeClass = std::max(sizeClass, deltaSizeClass(deltas[i], true));
        }

        uint64_t const countExp = exponentOfTwo(count);
        uint16_t const header = static_cast<uint16_t>((static_cast<uint16_t>(sizeClass) << 12) | (static_cast<uint16_t>(countExp) << 8));
        out.u16(header);

        uint32_t const byteSize = 1u << sizeClass;
        uint64_t const mask = maskForBytes(byteSize);

        for (size_t i = begin; i < begin + count; i++) {
            MacroEvent const& e = events[i];
            uint64_t const state = (deltas[i] << 4) | (static_cast<uint64_t>(static_cast<uint8_t>(e.type)) << 2) |
                                   (static_cast<uint64_t>(e.player2 ? 1 : 0) << 1) |
                                   static_cast<uint64_t>(e.holding ? 1 : 0);
            out.uintBytes(state & mask, byteSize);
        }
    }

    void writeSpecialSection(ByteWriter& out, MacroEvent const& e, uint64_t delta) {
        uint8_t const sizeClass = deltaSizeClass(delta, false);
        SpecialType const type = specialTypeOf(e.type);

        uint16_t const header = static_cast<uint16_t>(
            (static_cast<uint16_t>(SectionId::Special) << 14) | (static_cast<uint16_t>(type) << 10) |
            (static_cast<uint16_t>(sizeClass) << 8)
        );
        out.u16(header);
        out.uintBytes(delta, 1u << sizeClass);

        switch (type) {
            case SpecialType::Restart:
            case SpecialType::RestartFull:
            case SpecialType::Death: out.u64(e.seed); break;
            case SpecialType::TPS: out.f64(e.tps); break;
            case SpecialType::Bugpoint: break;
        }
    }

    // ---------------------------------------------------------------- decoding

    struct DecodeState {
        std::vector<MacroEvent>* events = nullptr;
        uint64_t lastFrame = 0;
    };

    void pushSwift(DecodeState& st, uint64_t frame, bool player2) {
        // A swift entry is a press and a release on the very same frame.
        MacroEvent press = MacroEvent::press(frame, Button::Jump, player2);
        MacroEvent release = MacroEvent::release(frame, Button::Jump, player2);
        st.events->push_back(press);
        st.events->push_back(release);
        st.lastFrame = frame;
    }

    void pushPlayer(DecodeState& st, uint64_t frame, uint8_t button, bool holding, bool player2) {
        MacroEvent e;
        e.frame = frame;
        e.type = static_cast<EventType>(button);
        e.holding = holding;
        e.player2 = player2;
        st.events->push_back(e);
        st.lastFrame = frame;
    }

    bool parseInputEntries(ByteReader& in, uint32_t byteSize, uint64_t length, DecodeState& st, std::string& error) {
        // Refuse absurd counts before allocating.
        if (length > in.remaining() / (byteSize ? byteSize : 1) + 1) {
            error = "input section claims more entries than the file can hold";
            return false;
        }
        for (uint64_t i = 0; i < length; i++) {
            auto state = in.uintBytes(byteSize);
            if (!state) {
                error = "unexpected end of file inside an input section";
                return false;
            }
            uint64_t const delta = *state >> 4;
            uint8_t const button = static_cast<uint8_t>((*state >> 2) & 0b11);
            bool const holding = (*state & 0b1) != 0;
            bool const player2 = (*state & 0b10) != 0;
            uint64_t const frame = st.lastFrame + delta;

            if (button == 0) {
                pushSwift(st, frame, player2);
            } else {
                pushPlayer(st, frame, button, holding, player2);
            }
        }
        return true;
    }

    bool parseSection(ByteReader& in, DecodeState& st, std::string& error) {
        auto header = in.u16();
        if (!header) {
            error = "unexpected end of file while reading a section header";
            return false;
        }

        SectionId const id = static_cast<SectionId>(*header >> 14);
        switch (id) {
            case SectionId::Input: {
                uint32_t const deltaSize = (*header >> 12) & 0b11;
                uint32_t const countExp = (*header >> 8) & 0b1111;
                uint32_t const byteSize = 1u << deltaSize;
                uint64_t const length = 1ull << countExp;
                return parseInputEntries(in, byteSize, length, st, error);
            }
            case SectionId::Repeat: {
                uint32_t const deltaSize = (*header >> 12) & 0b11;
                uint32_t const countExp = (*header >> 8) & 0b1111;
                uint32_t const repeatsExp = (*header >> 3) & 0b11111;
                uint32_t const byteSize = 1u << deltaSize;
                uint64_t const length = 1ull << countExp;
                uint64_t const repeats = 1ull << repeatsExp;

                if (length > in.remaining() / (byteSize ? byteSize : 1) + 1) {
                    error = "repeat section claims more entries than the file can hold";
                    return false;
                }
                if (length * repeats > 4'000'000'000ull) {
                    error = "repeat section expands to an implausible number of inputs";
                    return false;
                }

                // The block is stored with deltas relative to the block itself, so
                // it has to be read once and then re-anchored for every repetition.
                struct Entry {
                    uint64_t delta;
                    uint8_t button;
                    bool holding;
                    bool player2;
                };
                std::vector<Entry> block;
                block.reserve(static_cast<size_t>(length));

                for (uint64_t i = 0; i < length; i++) {
                    auto state = in.uintBytes(byteSize);
                    if (!state) {
                        error = "unexpected end of file inside a repeat section";
                        return false;
                    }
                    Entry e;
                    e.delta = *state >> 4;
                    e.button = static_cast<uint8_t>((*state >> 2) & 0b11);
                    e.holding = (*state & 0b1) != 0;
                    e.player2 = (*state & 0b10) != 0;
                    block.push_back(e);
                }

                for (uint64_t r = 0; r < repeats; r++) {
                    for (auto const& e : block) {
                        uint64_t const frame = st.lastFrame + e.delta;
                        if (e.button == 0) {
                            pushSwift(st, frame, e.player2);
                        } else {
                            pushPlayer(st, frame, e.button, e.holding, e.player2);
                        }
                    }
                }
                return true;
            }
            case SectionId::Special: {
                SpecialType const type = static_cast<SpecialType>((*header >> 10) & 0b1111);
                uint32_t const deltaSize = (*header >> 8) & 0b11;
                auto delta = in.uintBytes(1u << deltaSize);
                if (!delta) {
                    error = "unexpected end of file while reading a special section";
                    return false;
                }

                MacroEvent e;
                e.frame = st.lastFrame + *delta;
                e.type = eventTypeOf(type);

                switch (type) {
                    case SpecialType::Restart:
                    case SpecialType::RestartFull:
                    case SpecialType::Death: {
                        auto seed = in.u64();
                        if (!seed) {
                            error = "unexpected end of file while reading a seed";
                            return false;
                        }
                        e.seed = *seed;
                        break;
                    }
                    case SpecialType::TPS: {
                        auto tps = in.f64();
                        if (!tps) {
                            error = "unexpected end of file while reading a tps value";
                            return false;
                        }
                        e.tps = *tps;
                        break;
                    }
                    case SpecialType::Bugpoint: break;
                }

                st.events->push_back(e);
                st.lastFrame = e.frame;
                return true;
            }
            default: error = "unknown section id " + std::to_string(static_cast<int>(id)); return false;
        }
    }

} // namespace

bool encodeV3(Macro const& macro, V3Header const& header, std::vector<uint8_t>& out, std::string& error) {
    std::vector<MacroEvent> events = macro.events;
    std::stable_sort(events.begin(), events.end(), eventOrderLess);

    // Deltas are relative to the previous event, so they have to be computed on
    // the sorted stream before any sectioning happens.
    std::vector<uint64_t> deltas(events.size(), 0);
    uint64_t prevFrame = 0;
    for (size_t i = 0; i < events.size(); i++) {
        if (events[i].frame < prevFrame) {
            error = "event stream is not sorted by frame";
            return false;
        }
        deltas[i] = events[i].frame - prevFrame;
        prevFrame = events[i].frame;
    }

    // ---- action atom payload
    ByteWriter payload;
    payload.u64(static_cast<uint64_t>(events.size()));

    size_t i = 0;
    while (i < events.size()) {
        if (isSpecialEvent(events[i].type)) {
            writeSpecialSection(payload, events[i], deltas[i]);
            i++;
            continue;
        }

        // Longest run of player events starting here, capped to what a section
        // header can express (1 << 15 entries).
        size_t run = 0;
        while (i + run < events.size() && isPlayerEvent(events[i + run].type) && run < (1ull << 15)) run++;

        uint64_t const chunk = largestPowerOfTwo(run);
        writeInputSection(payload, events, deltas, i, static_cast<size_t>(chunk));
        i += static_cast<size_t>(chunk);
    }

    // ---- container
    ByteWriter w;
    w.raw(V3_MAGIC, sizeof(V3_MAGIC));
    w.u16(V3_METADATA_SIZE);

    double const tps = header.tps > 0.0 ? header.tps : macro.tps;
    w.f64(tps);
    w.u64(header.seed | macro.seed);
    w.u32(header.version);
    w.u32(header.build);
    w.u32(header.randomnessAlgorithm);
    w.pad(36); // metadata is a fixed 64 bytes

    w.u32(static_cast<uint32_t>(AtomId::Action));
    w.u64(static_cast<uint64_t>(payload.size()));
    w.raw(payload.bytes().data(), payload.size());

    w.u8(V3_FOOTER);

    out = w.take();
    return true;
}

bool decodeV3(std::vector<uint8_t> const& bytes, Macro& macroOut, V3Header* headerOut, std::string& error) {
    ByteReader in(bytes);

    uint8_t magic[8] = {0};
    for (int i = 0; i < 8; i++) {
        auto b = in.u8();
        if (!b) {
            error = "file is too short to be an .slc v3 replay";
            return false;
        }
        magic[i] = *b;
    }
    if (std::memcmp(magic, V3_MAGIC, 8) != 0) {
        error = "not an .slc v3 replay (bad magic)";
        return false;
    }

    auto metaSize = in.u16();
    if (!metaSize || *metaSize != V3_METADATA_SIZE) {
        error = "unsupported .slc v3 metadata size (expected 64)";
        return false;
    }

    V3Header header;
    if (auto tps = in.f64()) header.tps = *tps;
    if (auto seed = in.u64()) header.seed = *seed;
    if (auto version = in.u32()) header.version = *version;
    if (auto build = in.u32()) header.build = *build;
    if (auto algo = in.u32()) header.randomnessAlgorithm = *algo;
    if (!in.skip(36)) {
        error = "truncated metadata block";
        return false;
    }
    if (headerOut) *headerOut = header;

    Macro macro;
    macro.tps = header.tps > 0.0 ? header.tps : 240.0;
    macro.seed = header.seed;
    macro.source = "import";

    DecodeState st;
    st.events = &macro.events;

    // Atoms run until the single footer byte.
    while (in.remaining() > 1) {
        auto id = in.u32();
        auto rawSize = in.u64();
        if (!id || !rawSize) {
            error = "truncated atom header";
            return false;
        }
        uint64_t const size = *rawSize & ~(0xFFull << 56); // top byte holds flags
        size_t const atomStart = in.tell();

        if (static_cast<AtomId>(*id) == AtomId::Action) {
            auto count = in.u64();
            if (!count) {
                error = "truncated action atom";
                return false;
            }
            if (*count > 500'000'000ull) {
                error = "action atom claims an implausible number of actions";
                return false;
            }
            while (macro.events.size() < *count) {
                if (!parseSection(in, st, error)) return false;
            }
        } else {
            // Unknown / marker atoms are skipped, exactly like Silicate does.
            if (!in.skip(static_cast<size_t>(size))) {
                error = "atom size exceeds the remaining file";
                return false;
            }
        }

        // Make sure a malformed atom cannot desynchronise the stream.
        if (atomStart + static_cast<size_t>(size) > bytes.size()) {
            error = "atom size exceeds the remaining file";
            return false;
        }
        in.seek(atomStart + static_cast<size_t>(size));

        // Guarantees the atom loop always makes progress, so a truncated or
        // zero sized atom can never spin forever.
        if (in.tell() <= atomStart) {
            error = "malformed atom with an empty payload";
            return false;
        }
    }

    auto footer = in.u8();
    if (!footer || *footer != V3_FOOTER) {
        error = "missing .slc v3 footer byte";
        return false;
    }

    macro.normalize();
    if (!macro.events.empty()) macro.frameCount = macro.events.back().frame;
    for (auto const& e : macro.events) {
        if (e.type == EventType::TPS && e.tps > 0.0) macro.tps = e.tps;
    }

    macroOut = std::move(macro);
    return true;
}

} // namespace pathfinder::slc

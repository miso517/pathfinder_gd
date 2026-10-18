# Pathfinder

A Geode mod for **Geometry Dash 2.208 (Windows)** that finds a survivable path
through a level by *searching the real game engine*, then exports that path as a
[Silicate](https://github.com/silicate-bot) `.slc` macro.

The interesting design decision is what Pathfinder does **not** do: it does not
reimplement Geometry Dash physics. Reimplementing 2.2 physics (seven game modes,
slopes, dash orbs, moving objects, dual, platformer) is a large amount of code
that is always slightly wrong, and a slightly wrong simulator produces macros
that die in game. Instead Pathfinder drives the actual `PlayLayer` frame by
frame at a fixed tick rate and rewinds it with the game's own practice mode
checkpoints. Whatever the search concludes is therefore exactly what the game
will do, because it *is* the game doing it.

```
        ┌────────────────────────── search loop ──────────────────────────┐
        │                                                                 │
        │  generate candidates ──▶ rewind ──▶ step the real engine ──▶ score
        │        ▲                                              │         │
        │        │                                              ▼         │
        │        └──────────── backtrack ◀──────────── commit the best    │
        │                                                                 │
        └─────────────────────────────────────────────────────────────────┘
                                     │
                                     ▼
                    Macro (frame → button press/release)
                                     │
                    ┌────────────────┼────────────────┐
                    ▼                ▼                ▼
              .slc v3           .slc v2         JSON / TXT
```

## What it does

| Feature | How |
| --- | --- |
| **Find a path** | Rolling horizon search: for each window of *N* frames it tries a set of button patterns in the real engine, keeps the ones that survive, scores them by level progress and commits the best one. |
| **Escape traps** | When nothing survives, it backtracks to an earlier committed window and tries that window's next best candidate. This is what handles "the fatal mistake happens three jumps before the death". |
| **Verify** | Plays the finished macro back through the engine at its own tick rate and reports where (if anywhere) it dies. |
| **Record** | Turns your own run into a macro, using the same frame based format. |
| **Export** | `.slc` **v3** (default) or **v2**, plus a JSON and a TXT dump for debugging. Files go wherever you want through the system file picker, or straight into the mod's save folder. |
| **Import** | Reads any `.slc` (v3 or v2, including Silicate's run length encoded and "swift" sections) so you can play somebody else's macro or continue from it. |
| **Stay out of the way** | Search and playback run in practice mode with audio muted, so attempts, statistics and your volume settings are untouched. Bot runs never award level completion. |

## Using it

1. Enter a level.
2. Press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> (or the **Menu** button on
   the HUD in the top left) to open the control panel.
3. Press **Find Path**. The level restarts from the beginning and the HUD starts
   reporting progress: `searching 42.10% | 1.2M frames (5000.0s) | 3 backtracks`.
4. When it finishes, press **Export .slc** (choose a location) or **Quick
   Export** (drops it in the mod's save folder and opens it).
5. **Play Macro** runs the result back through the engine so you can watch it and
   confirm it survives.

<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd> starts/stops a search,
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>X</kbd> stops everything. All keybinds and
search parameters are in the mod's settings.

## How the search works

`src/core/Search.cpp` implements a **rolling horizon (model predictive) search**
over an abstract `Simulator`:

1. **Snapshot** the level state (`Snapshot`).
2. **Generate candidates** (`src/core/Plans.cpp`): coast, jump at every offset in
   the window with several hold lengths, release a button still held from the
   previous window, and directional input on platformer levels. The list is
   thinned to a budget with even sampling, so a small budget still covers the
   whole window.
3. **Evaluate**: for each candidate, rewind and step the window, then score it —
   `finished` ≫ `alive` (scored by progress, with a small bonus for ending on the
   ground) ≫ `dead` (progress × death penalty). A dead candidate therefore always
   loses to a survivable one that got just as far.
4. **Commit** the best survivor: rewind to the clean snapshot, replay it for real,
   append its inputs to the macro, snapshot again.
5. **Backtrack** when every candidate dies: pop the previous committed window and
   try *its* next best candidate, up to `backtrack-depth` windows back.
6. Stop on level completion, on the frame budget, or when the level really has no
   survivable continuation — in which case the partial macro is kept and exported.

Everything above is engine independent and unit tested; see
[Testing](#testing).

### Why two kinds of snapshot

Rewinding a whole level (`PlayLayer::loadFromCheckpoint`) is expensive, and the
search does it once per candidate. For levels whose state the player cannot
change, Pathfinder rewinds only the icons
(`PlayerObject::loadFromCheckpoint`), which is much cheaper. It decides
automatically: if the game's own checkpoint says the level has dynamic or
active save objects (breakables, toggles, pickups, moving blocks), the cheap path
is disabled and every candidate gets a full rewind. `fast-snapshots` in the
settings forces the safe path always.

## Layout

```
mod.json                  manifest, settings, keybinds
CMakeLists.txt            standard Geode mod build
src/
  main.cpp                keybinds
  hooks/Hooks.cpp         all $modify hooks (thin, they only forward)
  session/Session.*       controller: modes, sandbox, export/import, HUD
  sim/SimSignals.*        hook ⇄ simulator bridge
  sim/GDSimulator.*       drives a real PlayLayer as a steppable simulator
  core/Types.hpp          Macro / MacroEvent / Button
  core/Simulator.hpp      abstract steppable level
  core/Plans.*            candidate input generation
  core/Search.*           the rolling horizon search
  core/ByteIO.hpp         little endian buffer helpers
  format/SlcV3.*          .slc v3 codec (Silicate's current format)
  format/SlcV2.*          .slc v2 codec (legacy container)
  format/Slc.*            format sniffing + file IO
  format/Dump.*           JSON / TXT dumps
  ui/PathfinderPopup.*    control panel
  ui/HudLayer.*           in-level readout and stop button
tests/
  main.cpp                480+ assertions over the core and the codecs
  MockSimulator.hpp       deterministic stand-in for Geometry Dash
  run.sh                  builds and runs the suite with a plain compiler
```

`src/core` and `src/format` include nothing but the C++ standard library. That is
deliberate: it means the parts that are easy to get subtly wrong (the byte
format, the search) can be compiled and tested anywhere, and the game specific
code is a thin shell around them.

## Building

Windows is the only supported target: the practice mode checkpoint functions the
search rewinds with (`PlayLayer::createCheckpoint`,
`PlayerObject::loadFromCheckpoint`, …) are only bound for Windows in the 2.208
bindings. `mod.json` declares `"gd": { "win": "2.208" }` accordingly.

```bash
# 1. install the Geode CLI: https://docs.geode-sdk.org/getting-started/install
geode sdk install-release         # or: geode sdk install-nightly
geode sdk install-binaries -p win

# 2. point CMake at the SDK and build
export GEODE_SDK=$(geode sdk dir)
cmake -B build -G Ninja
cmake --build build --config Release
```

The `.geode` ends up in `build/` (`build/miso517.pathfinder.geode`). Install it
by dropping it into your `geode/mods` folder, or with `geode --install`.

On Windows the same thing works from a developer command prompt, provided the
[Geode installer](https://geode-sdk.org) has set up the SDK:

```bat
set GEODE_SDK=%USERPROFILE%\.geode
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Building on GitHub Actions

The workflow in `ci/build.yml` runs the core tests on Linux, builds the mod on
`windows-latest` with `geode-sdk/build-geode-mod`, uploads the `.geode` as the
`Pathfinder-Win64` artifact, and publishes it to the rolling `ci-latest`
release (tag pushes release under their tag instead).

It lives outside `.github/workflows/` because the automation token that pushes
this branch has no `workflows` permission and GitHub refuses to let it create
workflow files. Either grant that permission, or copy the file in yourself once:

```bash
mkdir -p .github/workflows && cp ci/build.yml .github/workflows/build.yml
git add .github && git commit -m "CI" && git push
```

After that every push builds it, and Actions → *Build Geode Mod* → any run →
**Artifacts** has the `.geode`.

## Testing

```bash
bash tests/run.sh
```

This compiles `src/core` and `src/format` with your local compiler (no SDK, no
game) and runs the suite:

* `.slc` v3 and v2 round trips, including deltas that cross every byte width
  boundary, TPS/restart/seed events, dual input, empty macros, hand built
  **repeat sections** and **swift** entries (so Silicate's own output parses),
  and rejection of truncated or lying containers.
* Plan generation: ordering, budget thinning, dual mirroring, hold carry over.
* Search: completes a jumpable level, uses full rewinds for dynamic levels,
  **backtracks out of a delayed death trap** (a level where the greedy choice
  survives six windows and then dies), reports failure on an impossible level,
  respects its frame budget, can be cancelled, survives being split into
  one-frame ticks, and leaks no snapshots in any of those paths.
* Every macro the search produces is replayed cold on a fresh simulator and has
  to finish the level, then survives an `.slc` round trip and has to finish
  again.

## Limitations (read this)

* **It searches greedily, not exhaustively.** It is a strong heuristic bot, not a
  solver with a completeness proof. On a level where the only route needs a
  specific setup ten seconds earlier than the death, `backtrack-depth` and
  `candidates` decide whether it finds it.
* **Non-cube modes are only as good as the candidate set.** The generator knows
  jump timing and (on platformer levels) left/right. Ship, wave, UFO and swing
  segments need sustained or rhythmic hold patterns; the horizon and hold length
  settings cover a lot of that, but dense ship spam is where this will run out of
  ideas first.
* **Moving objects, triggers and portals** are handled correctly because the real
  engine evaluates them — but they make the level "dynamic", which disables the
  cheap rewind and slows the search down.
* **Frame rate matters.** A search is thousands of engine steps per rendered
  frame. Expect the game to stutter and the level to flicker while it runs; that
  is the mod stepping the world, not a crash. Lower `frames-per-tick` if it is
  too much.
* **Bot runs never count.** Level completion is deliberately swallowed during
  search and playback, so no stars, no percentage, no leaderboard entry.
* **Other mods that hook gameplay** can change the physics the search measures.
  The macro is verified in *your* game with *your* mods loaded, which is the best
  guarantee available, but a macro exported from a modded client may not be
  frame accurate on a clean one.

## Credits

* `.slc` format: [silicate-bot/slc](https://github.com/silicate-bot/slc) — the
  codec here is an independent implementation of that container, written against
  its published headers.
* [Geode SDK](https://geode-sdk.org) and its
  [bindings](https://github.com/geode-sdk/bindings) for 2.208.
* [GucciBot](https://github.com/Nigelx1/GucciBot) as prior art for driving the
  real engine instead of reimplementing it.

## License

See [LICENSE](LICENSE).

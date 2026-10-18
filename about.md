# Pathfinder

Pathfinder plays a level for you: it searches for a route that survives, verifies
it against the real game, and saves it as a **Silicate `.slc` macro**.

## Quick start

1. Open any level.
2. Press <cg>Ctrl+Shift+P</c> (or tap **Menu** on the small HUD in the top left).
3. Press <cg>Find Path</c>. The level restarts in <cy>practice mode</c> and the
   HUD shows progress while the mod searches.
4. Press <cg>Export .slc</c> to choose where to save it, or <cg>Quick Export</c>
   to drop it in the mod's folder.
5. Press <cg>Play Macro</c> to watch the result run through the level.

<cg>Ctrl+Shift+F</c> starts and stops a search, <cg>Ctrl+Shift+X</c> stops
everything.

## Why it is accurate

Pathfinder does not guess how Geometry Dash physics work. It steps the **real**
level, one fixed tick at a time, and rewinds it with the game's own practice mode
checkpoints. Every candidate it considers has actually been played by the game
before it is kept, and the finished macro is played back the same way to verify
it.

## Good to know

- Search and playback run in practice mode with audio muted, so your attempts,
  statistics and volume settings are left alone.
- Bot runs <cr>never</c> award level completion: no stars, no percentage, no
  leaderboard entry.
- The game will stutter and the level will flicker while searching. That is the
  mod running thousands of physics ticks per rendered frame. Lower **Frames per
  rendered frame** in the settings if it is too much.
- If it stops early, the partial macro is still exported, and the notification
  tells you how far it got and why.
- Tune the search in the mod settings: **horizon** and **candidates** control how
  far ahead it looks and how many options it tries, **backtrack depth** controls
  how far back it can go to undo a mistake.

## Format

`.slc` version 3 by default (seeds, restarts and TPS changes supported), version
2 available for older tools. Importing reads either version, including macros
written by Silicate itself. JSON and TXT dumps are available for debugging.

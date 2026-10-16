# Changelog

## v1.0.0

* First release.
* Rolling horizon search that drives the real `PlayLayer` at a fixed tick rate and
  rewinds it with practice mode checkpoints, with backtracking across committed
  windows when every candidate dies.
* Automatic choice between a full rewind and a cheap icons-only rewind, based on
  whether the level contains state the player can change.
* Macro playback through the engine to verify a result, at real time or faster.
* Input recording: turn your own run into a macro.
* `.slc` v3 export (default) and v2 export, plus JSON and TXT dumps.
* `.slc` v3 and v2 import, including run length encoded sections and swift
  entries, so macros written by Silicate itself can be loaded and played.
* In-level HUD with live progress and a stop button, plus a control panel popup
  and three keybinds.
* Search and playback run in practice mode with audio muted; bot runs never award
  level completion.
* Engine independent core (`src/core`, `src/format`) with a unit test suite that
  runs without the game or the SDK.

# Support

## Reporting a problem

Open an issue on the
[tracker](https://github.com/miso517/pathfinder_gd/issues) and include:

1. **What you expected and what happened.** "Search stopped at 43% on Bloodbath"
   is useful; "it doesn't work" is not.
2. **The level.** A link or the level ID, and whether it is a normal level or
   platformer.
3. **The notification text** the mod showed when it stopped, or the HUD line at
   the moment you stopped it.
4. **Your settings** for `horizon`, `candidates`, `backtrack depth`, `tps` and
   `fast snapshots` (Geode → Mods → Pathfinder → settings).
5. **Your `geode.log`**, which is in your Geometry Dash folder under
   `geode/geode.log`. The mod logs when a search starts, how it was configured
   and why it stopped.
6. **Other mods you have loaded.** Anything that hooks gameplay, changes physics,
   or adds a TPS changer changes what the search measures. If you can reproduce
   with only Geode and Pathfinder loaded, say so — that is the most useful piece
   of information you can give.

If the search produced a macro that dies when you play it yourself, attach the
`.slc` (or the JSON dump from **JSON + TXT**): a macro that dies on playback is a
bug in this mod and the file is what makes it reproducible.

## Questions and discussion

Use the issue tracker for anything that needs a written answer. For quick
questions, the Geode Discord (`#modding`) is the best place — mention Pathfinder
and paste the same details as above.

## Before you report "it could not find a path"

That is a normal outcome, not necessarily a bug. Check first:

* Is the level actually survivable with the inputs the search can produce? It
  plans jump presses/releases, and left/right on platformer levels. Extremely
  dense ship, wave, UFO or swing spam can be beyond the candidate set.
* Try raising **Candidates per window** and **Search horizon**, and raising
  **Backtrack depth** if the death happens well after the mistake.
* Try raising **Total frame budget** — long levels can run out of budget before
  they run out of path.
* Levels with lots of moving objects, breakables or toggles are forced onto the
  slow full-rewind path. That is correct but slow; give it more time.

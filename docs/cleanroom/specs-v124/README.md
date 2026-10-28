# Clean-room specifications of v124

For the specification side only; the implementers never read this file.

- **Specification side.** It read the user's Rengine gates archive
  (Gates 17–24: their notes and scripts, made against the same `dmc3.exe`)
  and the vendored reader's headers, checked the rules against the stage
  files, and wrote the two specifications here in its own words. They name
  no addresses and contain no code from the executable or the archive.
  The research note with the addresses is
  `docs/research/dmc3-gates-archive-v124.md`.
- **Implementers.** One per specification. Each was given only its
  specification (and two Reader headers for code style). They were told not
  to open the research notes, the vendored reader, the tools or the archive.
  Each wrote its module and its test:

  | Specification | Module | Test |
  | --- | --- | --- |
  | `game_material.md` | `include/dmcresource/game_material.h`, `modules/game_material.cpp` | `game_material_test.cpp` |
  | `game_lights.md` | `include/dmcresource/game_lights.h`, `modules/game_lights.cpp` | `game_lights_test.cpp` |

- **The Reader's side.** Wiring the modules into the adapters, the frame and
  the GPU backend was then done against the modules' public interfaces.

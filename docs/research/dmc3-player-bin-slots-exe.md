# Player archive slots 9, 10 and 11 (dmc3.exe)

Read 2026-11-21 from the HD Collection's `dmc3.exe` (sha256 e454272e…),
static analysis only; rules in our own words, addresses only.

Every full player archive (pl000, pl001, pl002, pl011, pl013, pl021,
pl023, pl026) has, after the shadow (slot 8): slot 9 the `ff&A` table,
slot 10 and slot 11 two small tables with no signature. pl000 has its own
three; pl001/2/11/13 share theirs; the Vergil archives share slot 10 with
them and have their own slot 11.

## Where they go

The player init 0x140225D70 loads the archive slots through
0x1401B82C0 (archive, character id, slot) and keeps slot 9 at object
+0x3DE8, slot 10 at +0x3DF0, slot 11 at +0x3DF8 (a twin init 0x140219260
does the same for the second model set).

## Slot 10: the coat's motion states (Reader: COATST)

- A list of u16 pairs; it ends where the data begins (the lowest value,
  0x5C in pl000: 23 entries). The second half of entry i is the offset of
  record i, five floats; the first half (0x5C / 0x70) is never read by the
  consumers below.
- 0x1401F01F0 reads the motion script's opcode-3 signal every frame;
  channel 3 goes to +0x39C5 (channel 1 the weapon state, channel 2 the hand
  variant and effects).
- 0x1401FBD00 / 0x1401FBD90 (through the vtable) act when that byte is
  non-zero and differs from the last one (+0x3A13): they take record
  `state` and write into the coat cloth (the first 0xF0-byte chain at
  +0xA230): float 0 the stiffness (+0x6C), (0, float 1, 0) the wind
  (+0xA0), floats 2..4 the gravity (+0xB0). The values fit the coat's .clt
  (Stiffness 0.3, Gravity -0.05, WindLocal 1).
- pl000's script sets a coat state in 241 of its 769 actions (state 12
  most, then 2 and 1).

The Reader names the slot `.coatst`, shows the states in Info and, while a
scripted motion plays, gives the coat the state's parameters (state 0 keeps
the .clt's), as the game does.

## Slot 11: tuning constants (Reader: PLTUNE)

A flat table of floats and a few ints (at least 0x324 bytes used), read at
fixed offsets by the player's action handlers and the objects it owns
(0x1401FEFF0, 0x1401FF0C0, 0x1401FF190 read +0x318..+0x320 for
0x1401FAD70; 0x1401D85D0 counts down +0xB8 / +0xBC; 0x1401DAB60 +0xFC).
Durations, speeds, ranges: gameplay only, nothing drawn depends on it. The
Reader names it `.pltune` and lists its values.

## Slot 9

Also a tuning table (0x1401FAD70 compares a distance with +0xDC).

## Open

The exact role of the two consumer twins (two chains, or current and
target), the first halves of slot 10's entries, enemy equivalents.

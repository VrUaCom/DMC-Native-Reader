# DMC Native Reader for Android — v39 / 1.0.12

Read-only viewer for Devil May Cry 3 resources. Opens files and shows them;
never modifies, repacks or writes game data.

## New in this build

- **MOT playback**: motions from a PAC play on the assembled character.
- **PAC assembly**: a `pl*.pac` opens as the full character. The coat
  (slot 12) hangs from body joint 3 and uses the body texture.
- **Weapons**: `plwp_*.pac` (PNST containers) open on their own. Open a
  character, then use `⋮ → Add weapon / .PAC…`: Rebellion, Agni & Rudra,
  Nevan, Yamato and the others attach where the game puts them when sheathed.
- **Nevan (`em028.pac`)**: body, hair, bat dress and bat sleeves are placed
  node by node as the game's constraints place them. Effect models (slash
  trails, sparks, bats) are skipped.
- **Rotation order fix**: MOD rest rotations now follow the game (X, then Y,
  then Z). This mostly affects poses that rotate around more than one axis.
- **Community-made textures**: PTX files written by community tools are shown
  with an orange ▲ that marks them as not read canonically.
- Drag now turns the model the way the finger moves.

## Not yet

- Cloth, hair and bat-chain physics (coats and Nevan's dress keep their rest
  shape).
- Weapons held in the hands (only the sheathed state).
- Node constraints of enemies other than Nevan.

## Install

Download `DMC-Native-Reader-v39-1.0.12-<commit>.apk` on the phone, open it and
allow installing from this source when Android asks.

## Package

- `arm64-v8a`, Android 8.0+ (minSdk 26, targetSdk 36), one native library.
- Signed with the repository's stable **test** key, so it installs over
  earlier test builds. It is not a store-signed production build.
- Not yet tested on a device by the build pipeline.
- The attached `.sha256` file holds the checksum of the APK.

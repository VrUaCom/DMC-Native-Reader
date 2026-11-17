# DMC Native Reader for Android — v151 (1.0.124)

A device-test APK built from the `NR-Luna-v73` source branch.

- **Package:** `com.dmcrengine.nativereader`
- **ABI:** `arm64-v8a`
- **versionCode:** `151`
- **versionName:** `1.0.124`
- **Signing:** the repository's stable debug/device-test signer, so it installs
  over earlier test builds. This is not a production release.

## Since v149 (1.0.122)

- An SCM whose streams are laid out mesh by mesh (written by a modding tool)
  opens as its model, with an orange non-canonical note (v150).
- The reader no longer closes without a trace (v151):
  - a native fault or a Java exception leaves a note; the next start shows
    one report (device, version, the file being opened, the call stack)
    with a Share button;
  - the file it closed on opens as its description, nothing parsed or
    drawn, with "Draw this file again";
  - Settings has "Reset saved data", the same as a reinstall.
- Fixes: animation card names are cut by characters, not bytes; every string
  from the core to Java is checked as UTF-8; the open plan runs under the
  session lock again.
- Checked on an Android 11 emulator before release
  (`tools/emulator_smoke.sh`): start, opening MOD, SCM and PAC files, and
  the crash notes with the description reopen. Not yet on a phone.

See `docs/STATUS.md` for every version.

The APK and its SHA-256 are produced by the repository's Android release
workflow from the commit that adds these notes.

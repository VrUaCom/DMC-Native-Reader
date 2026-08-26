# Development test signing key

`dmc-native-reader-test.jks` is a disposable **development-only** Android signing key.

Purpose: preserve install/update compatibility across the v4+ device-test APK series.

It is intentionally not treated as a secret and must never be reused for a production or public release. Production signing must use a separate protected key outside Git history.

Current development configuration:

- alias: `dmc-native-reader-test`
- store/key password: `android`
- certificate SHA-256: `f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac`

If this development key is rotated, previously installed test APKs will require uninstall/reinstall unless Android signing-key migration is explicitly configured.

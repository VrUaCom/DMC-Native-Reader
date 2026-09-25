chrome://flags/#reduced-referrer-granularity

Last updated: 2026-09-10.

## Supported line

Security fixes should target the current accepted Native Reader v1 `main` line and its direct active successor. The accepted Android baseline is versionName `1.0`, versionCode `24`; development candidates such as v26 remain pre-acceptance until promoted.

## Reporting a security issue

Do **not** publish exploit details, malicious sample files, signing material or sensitive device information in a public issue.

Prefer GitHub private security reporting/security advisories when enabled. If no private reporting channel is available, contact the repository owner through GitHub before publishing technical exploit details.

Useful reports include:

- affected version/commit;
- Android version/device class;
- exact supported resource family/path involved;
- crash vs memory-safety vs routing vs signing vs resource-exhaustion issue;
- smallest reproducible input you are legally allowed to share;
- whether the issue is reachable through Android `Open with` / SAF or only direct in-app navigation.

## Parser and resource security model

DMC Native Reader opens untrusted binary data and treats parser safety as a product requirement.

Current production boundaries include:

- read-only source handling;
- bounded native resource mapping (512 MiB cap);
- explicit production routing limited to MOD / SCM / DDS / PTX;
- fail-closed unknown/unpromoted families;
- format-specific bounds/overflow validation;
- bounded image/preview allocation paths;
- no wildcard parser that guesses arbitrary layouts;
- no Java/Kotlin DMC binary parsing;
- no stale render state reuse after rejected/non-renderable sessions;
- child/session lifetime owned by portable native session logic;
- texture companion replacement fails closed without destroying previously valid state.

A successful parse never grants permission to modify the source file. Native Reader remains read-only.

## Native interface boundary

The accepted v24 Android shared library exposes only the declared JNI entry-point surface rather than the implementation's general C++ symbols. APK verification checks this boundary together with package, ABI, ZIP, module markers and signing expectations.

Any new JNI method must be intentional, declared and reflected in verification. Do not expose a general native plugin ABI accidentally.

## Signing boundary

`keys/dmc-native-reader-test.jks` is a **development/test-only** signing key used for internal/device-test install-over continuity. It is not production trust material.

Official distribution must use a separate protected production signing authority stored outside Git history. Production private keys/passwords must not be committed to this repository.

Normal Gradle release output remains unsigned unless the protected production release path explicitly injects signing material.

## Scope

In scope:

- Native Reader parser/decoder memory safety;
- malicious/malformed MOD/SCM/DDS/PTX handling;
- Android intent/file-opening attack surface;
- native session/child lifetime issues;
- image/texture allocation/resource exhaustion;
- signing/build-pipeline issues that affect Native Reader trust or distribution.

Out of scope unless they directly affect Native Reader interoperability/security:

- vulnerabilities in Capcom software;
- vulnerabilities in the original game executable;
- unrelated third-party modding tools.
chrome://flags/#reduced-referrer-granularity

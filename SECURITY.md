# Security Policy

## Supported line

The actively supported security/debug line is the current Native Reader v1 development branch and its direct successors.

## Reporting a security issue

Please do **not** publish exploit details, malicious sample files, signing material or sensitive device information in a public issue.

Prefer GitHub's private security-reporting / security-advisory channel when available. If private reporting is not enabled, contact the repository owner through GitHub before posting technical details publicly.

Useful security reports include:

- affected version/commit;
- Android version/device class;
- exact file type and triggering condition;
- crash vs memory-safety vs path/routing vs signing issue;
- smallest reproducible input you are legally allowed to share;
- whether the issue is reachable through Android `Open with` / SAF.

## Parser security model

DMC Native Reader opens untrusted binary data and therefore treats parser safety as a product requirement.

Current boundaries include:

- read-only input access;
- bounded native mapping (512 MiB cap);
- explicit family routing;
- fail-closed unknown-family behavior;
- format-specific bounds validation;
- no wildcard parser that attempts arbitrary layouts;
- non-renderable sessions cannot reuse stale geometry.

A successful parse is not considered permission to write back to the source file.

## Signing boundary

`keys/dmc-native-reader-test.jks` is a **disposable development-only signing key** used by the historical debug APK series.

It is intentionally not a production secret and must never be trusted as the signer for an official public distribution build.

Production signing must use a separate protected key stored outside Git history (for example through protected CI secrets / environment configuration).

Users of development-signed APKs should treat them as test builds, not as a production trust chain.

## Scope

Security reports about the Native Reader application, parser memory safety, Android intent routing or build/signing pipeline are in scope.

Issues in Capcom software, the game executable or third-party modding tools are outside this repository's security scope unless they directly affect DMC Native Reader interoperability.

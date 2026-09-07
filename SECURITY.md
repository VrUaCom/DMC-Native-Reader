# Security Policy

## Supported line

Security fixes target the current `main` branch and the latest stable v1 release unless a newer supported line is documented.

The stable v1.0.0 distribution is Android. Future iOS, Windows and Web shells become supported only when explicitly released as supported product lines.

## Reporting a security issue

Please do **not** publish exploit details, malicious sample files, private signing material, tokens, passwords or sensitive device information in a public issue.

Use GitHub private vulnerability reporting / security advisories when enabled. If private reporting is unavailable, contact the repository owner through GitHub before posting technical details publicly.

Useful reports include:

- affected version / commit;
- platform, OS version and device class;
- exact file type and triggering condition;
- crash vs memory-safety vs path/routing vs signing issue;
- smallest reproducible input you are legally allowed to share;
- whether the issue is reachable through the platform's normal file-opening path;
- for Android v1, whether it is reachable through `Open with` / SAF.

## Parser security model

DMC Native Reader opens untrusted binary data and treats parser safety as a product requirement.

Current boundaries include:

- read-only input access;
- bounded native mapping;
- explicit family routing;
- exactly MOD / SCM / DDS / PTX in the stable v1 registry;
- fail-closed unknown/unpromoted formats;
- format-specific bounds validation;
- no wildcard parser that attempts arbitrary layouts;
- non-renderable sessions cannot reuse stale geometry;
- platform shells do not own independent copies of DMC binary semantics.

A successful parse is not permission to write back to the source file.

## DMC Rengine authority boundary

Reusable engine-level parsers, typed semantics, validation and runtime knowledge should be promoted in DMC Rengine when they belong to the central modding core. Native Reader platform shells must not silently diverge into independent semantic implementations.

For the future Web shell, DMC resource semantics are intended to execute in C++20/WebAssembly rather than in a second JavaScript/TypeScript parser stack.

## Signing boundary

No Android production private key may be committed to this repository.

Public-source Android debug builds use:

- package: `com.dmcrengine.nativereader.debug`;
- Android's ordinary local debug signer;
- no trust relationship with the production package.

The production Android application uses:

- package: `com.dmcrengine.nativereader`;
- versionName `1.0.0`, versionCode `20` for the first stable release;
- a dedicated protected signing authority;
- signing material injected only through protected CI/environment secrets.

Pinned v1 production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

Accepted v1.0.0 APK SHA-256:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

The protected release workflow verifies the certificate fingerprint before an APK is accepted. It must never upload keystores or key passwords as artifacts.

The historical development test signer is retired. It must not be treated as an official trust root even if it remains recoverable from old private-development history.

## Secret-handling rule

Never commit or attach to public issues/PRs:

- `.jks`, `.keystore`, `.p12`, `.pfx` or private-key files;
- release passwords/passphrases;
- GitHub/API tokens;
- credentials from local machines;
- production signing backups.

If any production credential is exposed, stop release work and rotate/revoke it before continuing.

## Scope

Reports about the Native Reader application, parser memory safety, platform file routing, dependency/build security or release/signing pipeline are in scope.

Issues in Capcom software, the original game executable or unrelated third-party modding tools are outside this repository's security scope unless they directly affect DMC Native Reader interoperability.

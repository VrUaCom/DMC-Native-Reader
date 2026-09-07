# Security Policy

## Supported line

Security fixes target the current `main` branch and the latest stable v1 release unless a newer supported line is documented.

## Reporting a security issue

Please do **not** publish exploit details, malicious sample files, private signing material, tokens, passwords or sensitive device information in a public issue.

Use GitHub private vulnerability reporting / security advisories when enabled. If private reporting is unavailable, contact the repository owner through GitHub before posting technical details publicly.

Useful reports include:

- affected version / commit;
- Android version and device class;
- exact file type and triggering condition;
- crash vs memory-safety vs path/routing vs signing issue;
- smallest reproducible input you are legally allowed to share;
- whether the issue is reachable through Android `Open with` / SAF.

## Parser security model

DMC Native Reader opens untrusted binary data and treats parser safety as a product requirement.

Current boundaries include:

- read-only input access;
- bounded native mapping;
- explicit family routing;
- fail-closed unknown/unpromoted formats;
- format-specific bounds validation;
- no wildcard parser that attempts arbitrary layouts;
- non-renderable sessions cannot reuse stale geometry.

A successful parse is not permission to write back to the source file.

## Signing boundary

No Android production private key may be committed to this repository.

Public-source debug builds use:

- package: `com.dmcrengine.nativereader.debug`;
- Android's ordinary local debug signer;
- no trust relationship with the production package.

The production application uses:

- package: `com.dmcrengine.nativereader`;
- a dedicated protected signing authority;
- signing material injected only through protected CI/environment secrets.

Pinned v1 production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

The protected release workflow verifies this fingerprint before publishing a release artifact. It must never upload keystores or key passwords as artifacts.

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

Reports about the Native Reader application, parser memory safety, Android intent routing, dependency/build security or release/signing pipeline are in scope.

Issues in Capcom software, the original game executable or unrelated third-party modding tools are outside this repository's security scope unless they directly affect DMC Native Reader interoperability.

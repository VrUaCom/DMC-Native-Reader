# Desktop CI (ready to enable)

`desktop.yml` builds the Win32 shell and CLI with MinGW (UCRT64) and MSVC,
runs the host tests on both, and builds the macOS app. The agent's GitHub
token has no `workflow` scope, so it could not add the file under
`.github/workflows/`. To enable it:

```sh
gh auth refresh -s workflow
git mv docs/port/ci/desktop.yml .github/workflows/desktop.yml
git commit -m "CI: desktop shells" && git push
```

It runs on pushes to NR-Luna-v73 that touch the core, the tests or `apple/`,
and by hand (workflow_dispatch). Nothing is signed or published.

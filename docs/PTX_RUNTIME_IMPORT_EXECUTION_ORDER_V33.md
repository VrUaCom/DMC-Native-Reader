# PTX Runtime Import Execution Order v33

1. Freeze current PTX serialized-path baseline.
2. Review authorized source snapshot `50d070e...` read-only.
3. Approve minimum import inventory.
4. Port minimum runtime slice to C++23 Reader-owned implementation.
5. Add thin wrapper and lazy integration.
6. Add bounded regressions to existing test graph.
7. Static exception/noexcept + duplicate/weight review.
8. PTX-specific review gate.
9. Fold accepted PTX changes into global Phase-2 exact-head evidence.
10. Run the single batched build/CTest/APK/verifier checkpoint when migration source is complete.

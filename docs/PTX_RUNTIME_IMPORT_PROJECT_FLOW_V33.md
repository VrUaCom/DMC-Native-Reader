# PTX Runtime Import — Project Flow v33

The PTX runtime import is a specially authorized exception inside the ongoing C++23 migration program.

It does not replace Phase 2; it inserts a reviewed PTX track before the global Phase-2 review.

```text
#36 Phase 2 C++23 source transition
    |
    +-- #49 exception/noexcept hardening
    +-- PTX Architecture Review
            |
            v
        PTX Runtime Import / C++23 Integration
            |
            v
        PTX Runtime Review Gate
            |
            v
#41 Global Review Gate A
    |
    v
#37 Bounded C++23 modernization
```

The PTX review issues must update the next task before implementation continues. A PTX `NO-GO` returns work to the PTX integration task and blocks #41 from declaring the whole Phase-2 candidate clean.

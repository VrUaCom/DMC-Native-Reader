# PTX Runtime Import Scope Guard v33

Allowed source reference: read-only `VrUaCom/dmc-rengine-cpp@50d070e158e484937238d9cb02b2bc6affb2f502`.

Allowed import categories:
- runtime PTX manager state needed by confirmed behavior;
- runtime PTX pool state needed by confirmed behavior;
- initialization;
- reservation configuration;
- placement;
- confirmed cache-key/count reset behavior;
- confirmed payload/occupancy preservation semantics;
- confirmed acquire/release/reset behavior present in the authorized slice.

Not allowed without a later explicit review/update:
- unrelated Rengine code;
- reverse harness infrastructure;
- EXE dump tooling;
- unrelated resource formats;
- guessed palette behavior;
- guessed finalizer/cleanup ordering;
- a second serialized PTX parser.

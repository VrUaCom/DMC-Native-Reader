# PTX Runtime Import Notes v33

This file intentionally contains no copied reverse code. It records integration constraints only.

Do not paste Rengine code into documentation. Copy only the minimum production implementation into bounded Native Reader source files after the architecture review GO.

The implementation must preserve:
- existing PTX serialized parse/decode behavior;
- existing preview/gallery/PNG outputs;
- source PTX bytes;
- one-module user experience;
- lazy runtime activation;
- C++23 fail-closed/error contracts;
- size/dedup limits.

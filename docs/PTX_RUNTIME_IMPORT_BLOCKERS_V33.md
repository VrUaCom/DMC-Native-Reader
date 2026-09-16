# PTX Runtime Import Blockers v33

Hard blockers:
- any required write to `dmc-rengine-cpp`;
- need for behavior not present/confirmed in authorized source evidence;
- introduction of a second serialized PTX parser;
- guessed palette/finalizer/cleanup semantics;
- duplicate runtime implementations;
- violation of C++23 exception boundary policy;
- unavoidable package/device size violation without owner limit change;
- regression in existing PTX/DDS/preview/render behavior.

A blocker must be recorded in Project and reviewed; do not bypass it with heuristic behavior.

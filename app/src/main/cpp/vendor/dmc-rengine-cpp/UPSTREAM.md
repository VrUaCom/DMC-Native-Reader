# Vendored canonical reader core

Source repository: `VrUaCom/dmc-rengine-cpp`

Pinned upstream commit:

`c72b7b056517039c815ee4322119f9eb40589601`

Policy:

- files under this directory are copied from the pinned canonical reverse/evidence source;
- do not patch vendored format code locally to make Android behavior diverge;
- product-specific conversion belongs in Native Reader adapters outside this vendor tree;
- updates must name the new upstream commit and re-run host + Android regression gates;
- the first imported slice is the bounded MOD structural reader and its minimal dependencies.

Initial upstream blobs:

- `include/dmc_rengine/binary/reader.hpp` — `43d721a9dd83d171184ecfabac6ca5d758ac2130`
- `src/binary/reader.cpp` — `63ff1b0eb6a05375eda16757425df9bcf41eff9e`
- `include/dmc_rengine/formats/diagnostic.hpp` — `2c66b7b38462bea096046404e39e1146a8c0d187`
- `include/dmc_rengine/formats/model_mesh_core.hpp` — `805bda2d8dc80d4ce3b9b04fbe587930a834eeaa`
- `include/dmc_rengine/formats/mod_skin.hpp` — `d4ba6cb33ff7cd3d8da067fe0263f5f56c74ca46`
- `src/formats/mod_skin.cpp` — canonical file at pinned upstream commit
- `include/dmc_rengine/formats/mod/transform_domain.hpp` — `be43b3c69d8f9144e0db10f86d86af414727d7a0`
- `src/formats/mod/transform_domain.cpp` — canonical file at pinned upstream commit
- `include/dmc_rengine/formats/mod.hpp` — `839a87727eee7c1493eaad8bdf579d25354b6be1`
- `src/formats/mod.cpp` — `596e2b01cd03eeeafe4111282c204473860cf296`

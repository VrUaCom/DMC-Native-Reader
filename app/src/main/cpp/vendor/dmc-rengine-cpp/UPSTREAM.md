# Vendored canonical reader core

Source repository: `VrUaCom/dmc-rengine-cpp`

Current canonical MOD pin (`main`, after PR #307 typed material-state promotion):

`3a203b016bcd651fba4a8ca11b4939d663f98468`

Previous Native Reader MOD pin:

`84d80b98103a2b0bd365c5b911209763edd17204`

PR #305 consolidated MOD spatial hierarchy into canonical `main`. PR #307 then promoted the already EXE-confirmed MOD texture-slot and legacy GS CLAMP/REGION_REPEAT payload into typed `mod::InnerMesh`. The Native Reader MOD read-side slice below is content-identical to files reachable from the single canonical main commit above. Vendored files remain unmodified; Android-specific projection belongs outside this vendor tree.

Policy:

- files under this directory are copied byte/text-identically from the named canonical reverse/evidence source;
- do not patch vendored format code locally to make Android behavior diverge;
- product-specific conversion belongs in Native Reader adapters outside this vendor tree;
- updates must name the upstream commit and re-run host + Android regression gates;
- reader slices deliberately exclude canonical writer/authoring modules unless a Native Reader feature requires them.

## MOD reader slice — `main@3a203b016bcd651fba4a8ca11b4939d663f98468`

- `include/dmc_rengine/binary/reader.hpp` — `43d721a9dd83d171184ecfabac6ca5d758ac2130`
- `src/binary/reader.cpp` — `63ff1b0eb6a05375eda16757425df9bcf41eff9e`
- `include/dmc_rengine/formats/diagnostic.hpp` — `2c66b7b38462bea096046404e39e1146a8c0d187`
- `include/dmc_rengine/formats/model_mesh_core.hpp` — `805bda2d8dc80d4ce3b9b04fbe587930a834eeaa`
- `include/dmc_rengine/formats/model_node_domain_core.hpp` — `cecae752b294913bca74631dd59df5baf68972f3`
- `include/dmc_rengine/formats/mod_skin.hpp` — `d4ba6cb33ff7cd3d8da067fe0263f5f56c74ca46`
- `src/formats/mod_skin.cpp` — `69d711a9f09465861a08a50252b9c00436943b21`
- `include/dmc_rengine/formats/mod/transform_domain.hpp` — `9555c23d3caed786ae7837941d50e5a0d0d8dc3f`
- `src/formats/mod/transform_domain.cpp` — `8d7be95f41d1694045d28d6b9cfdcae5f08f543b`
- `include/dmc_rengine/formats/mod/world_transform.hpp` — `35c222143536e48349b56fc1f3d6aec1fd0d5c5d`
- `src/formats/mod/world_transform.cpp` — `fabb723bb7b4d8c1ee5795a6c51f32c5f1fa7ad3`
- `include/dmc_rengine/formats/mod.hpp` — `f708f05a04682250692ab0effa3759ba0875e1dc`
- `src/formats/mod.cpp` — `3a4862b1b7c23c9ad70253e1923f1419899ea6b6`

Evidence boundary for the MOD spatial slice:

- `parentByOrderPosition` / `nodeAtOrderPosition`: EXE_CONFIRMED;
- serialized 0x20 local transforms: EXE_AND_CORPUS_CONFIRMED;
- model-space world propagation: EXE_CONFIRMED;
- node position authority: world matrix row 3 XYZ when `supports_spatial_hierarchy()` passes;
- adapter-domain `+0x08`: PRESERVED_UNDECODED;
- transform writer/mutation safety: not promoted;
- game-world placement beyond identity/model-space root: not inferred.

Evidence boundary for the MOD material-state slice:

- texture slot `+0x02`: EXE_CONFIRMED through the shared MOD/EFM/SCM material helper `0x1402F9890`;
- legacy GS CLAMP REGION_REPEAT fields `+0x04/+0x06/+0x08/+0x0A`: EXE_CONFIRMED through the same helper;
- typed fields are parsed during the existing single MOD parser pass;
- values outside the 10-bit GS register domain are preserved as raw u16 with diagnostics and are not masked;
- mapping from the MOD runtime texture slot to an actual bitmap/companion payload remains unresolved in this slice;
- no modern material names, filter semantics, shader semantics, or writer behavior are inferred.

## SCM reader slice

The SCM import contains only the canonical structural/read-side closure: parser, layout validation, topology, transform and hierarchy helpers. Writer/authoring code is intentionally not vendored into Native Reader.

- `include/dmc_rengine/formats/scm.hpp` — `9ae5b8575b66080f0a8925c59a615bf8c96c5219`
- `include/dmc_rengine/formats/scm_render.hpp` — `0c497ded59816a16f7d8d2240ef595995904637c`
- `include/dmc_rengine/formats/scm_resource_code.hpp` — `f9bde16aa3275d48d7d31d7a57b6244c259e6077`
- `include/dmc_rengine/formats/scm_layout.hpp` — `e9f8fffde0e6805b24e62b32e7d3480a7f040cd2`
- `include/dmc_rengine/formats/scm_topology.hpp` — `bd373d9e63cdb35cec1f976f3a0a748c51e3bcb7`
- `include/dmc_rengine/formats/scm_transform.hpp` — `e4bfb52f23439cfcce5c58b3631b381c6e4a5187`
- `include/dmc_rengine/formats/scm_hierarchy.hpp` — `2c0e9dd43a9b110cf26ab99bef29432802fefe52`
- `src/formats/scm_internal.hpp` — `46d936dd60166c45c90be9089c574cc9de9b069c`
- `src/formats/scm_layout.cpp` — `03563d931136bb8a5436c2d9a53c75fb20f73f87`
- `src/formats/scm_topology.cpp` — `26e910488ee7a4b8f40572ad3e7be03ad20d8212`
- `src/formats/scm_validation.cpp` — `aef01676cab6151c43066871fc3ca3ed2044e917`
- `src/formats/scm_transform.cpp` — `3b0bff72137a0b5cca924f3e0767eb840d361d4a`
- `src/formats/scm_hierarchy.cpp` — `9cd38e59ed95d63baf7bbd912d73c7ca9cb4c969`
- `src/formats/scm.cpp` — `ed0aec2ac8c4648baa517bc439885b628b854e69`

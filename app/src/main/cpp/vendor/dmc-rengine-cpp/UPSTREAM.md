# Vendored canonical reader core

Source repository: `VrUaCom/dmc-rengine-cpp`

Base reader pin (`main`):

`3a3db646c6bf3faf1871efbed31c4e9f4fb32cbe`

MOD spatial-hierarchy authority pin (`mod-skin-reverse`, merged PR #302):

`cb30171ef5fd38060f5a1af30f12858aa29b75dd`

Previous content-equivalent MOD base pin:

`c72b7b056517039c815ee4322119f9eb40589601`

The Native Reader vendors a minimal read-side slice. Provenance is recorded per file because the canonical MOD spatial-hierarchy promotion was merged into the dedicated `mod-skin-reverse` branch after the structural MOD reader had already been pinned from canonical `main`. Vendored files are copied byte/text-identically; Android-specific projection remains outside the vendor tree.

Policy:

- files under this directory are copied byte/text-identically from the named canonical reverse/evidence commits;
- do not patch vendored format code locally to make Android behavior diverge;
- product-specific conversion belongs in Native Reader adapters outside this vendor tree;
- updates must name the upstream commit and re-run host + Android regression gates;
- reader slices deliberately exclude canonical writer/authoring modules unless a Native Reader feature requires them.

## MOD reader slice

Base structural reader files from `main@3a3db646c6bf3faf1871efbed31c4e9f4fb32cbe` unless explicitly marked otherwise:

- `include/dmc_rengine/binary/reader.hpp` — `43d721a9dd83d171184ecfabac6ca5d758ac2130`
- `src/binary/reader.cpp` — `63ff1b0eb6a05375eda16757425df9bcf41eff9e`
- `include/dmc_rengine/formats/diagnostic.hpp` — `2c66b7b38462bea096046404e39e1146a8c0d187`
- `include/dmc_rengine/formats/model_mesh_core.hpp` — `805bda2d8dc80d4ce3b9b04fbe587930a834eeaa`
- `include/dmc_rengine/formats/mod_skin.hpp` — `d4ba6cb33ff7cd3d8da067fe0263f5f56c74ca46`
- `src/formats/mod_skin.cpp` — `69d711a9f09465861a08a50252b9c00436943b21`
- `include/dmc_rengine/formats/mod.hpp` — `839a87727eee7c1493eaad8bdf579d25354b6be1`
- `src/formats/mod.cpp` — `596e2b01cd03eeeafe4111282c204473860cf296`

Spatial hierarchy/local-world transform files from `mod-skin-reverse@cb30171ef5fd38060f5a1af30f12858aa29b75dd`:

- `include/dmc_rengine/formats/model_node_domain_core.hpp` — `cecae752b294913bca74631dd59df5baf68972f3`
- `include/dmc_rengine/formats/mod/transform_domain.hpp` — `9555c23d3caed786ae7837941d50e5a0d0d8dc3f`
- `src/formats/mod/transform_domain.cpp` — `8d7be95f41d1694045d28d6b9cfdcae5f08f543b`
- `include/dmc_rengine/formats/mod/world_transform.hpp` — `35c222143536e48349b56fc1f3d6aec1fd0d5c5d`
- `src/formats/mod/world_transform.cpp` — `fabb723bb7b4d8c1ee5795a6c51f32c5f1fa7ad3`

Evidence boundary for the spatial slice:

- `parentByOrderPosition` / `nodeAtOrderPosition`: EXE_CONFIRMED;
- serialized 0x20 local transforms: EXE_AND_CORPUS_CONFIRMED;
- model-space world propagation: EXE_CONFIRMED;
- node position authority: world matrix row 3 XYZ when `supports_spatial_hierarchy()` passes;
- adapter-domain `+0x08`: PRESERVED_UNDECODED;
- transform writer/mutation safety: not promoted;
- game-world placement beyond identity/model-space root: not inferred.

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

// Borrows the MOD/SCM fixture builders from the canonical Core regression
// instead of duplicating them. The canonical test file is compiled unchanged;
// only its entry point is renamed so it does not collide with this binary's.
#define main dmc_ios_embedded_core_model_pipeline_main
#include "../../app/src/test/native/core_model_pipeline_test.cpp"
#undef main

std::vector<std::uint8_t> bridge_fixture_scm() { return make_scm(); }
std::vector<std::uint8_t> bridge_fixture_mod() { return make_mod(); }

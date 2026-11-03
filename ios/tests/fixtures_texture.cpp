// Borrows the DDS/PTX fixture builders from the canonical Core regression;
// see fixtures_model.cpp. A separate translation unit keeps the two test
// files' anonymous-namespace helpers from colliding.
#define main dmc_ios_embedded_dds_ptx_v1_main
#include "../../app/src/test/native/dds_ptx_v1_test.cpp"
#undef main

std::vector<std::uint8_t> bridge_fixture_dds() { return make_dds(); }
std::vector<std::uint8_t> bridge_fixture_ptx() { return make_ptx_zero_final_span(); }

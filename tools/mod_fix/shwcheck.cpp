#include "dmc_rengine/formats/shw.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
int main(int, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
    unsigned n; std::memcpy(&n, &b[4], 4);
    for (unsigned i = 0; i < n; ++i) {
        unsigned o; std::memcpy(&o, &b[8 + 4 * i], 4);
        if (std::memcmp(&b[o], "SHW ", 4) != 0) continue;
        unsigned e = b.size();
        for (unsigned k = 0; k < n; ++k) { unsigned q; std::memcpy(&q, &b[8 + 4 * k], 4); if (q > o && q < e) e = q; }
        auto r = dmc::rengine::formats::shw::Parser::parse({reinterpret_cast<const std::byte*>(&b[o]), e - o});
        std::printf("slot %u: ok=%d hulls=%zu diagnostics=%zu\n", i, (int)r.ok(), r.document.hulls.size(), r.diagnostics.size());
        for (auto& d : r.diagnostics) std::printf("   %s @%llu\n", d.code.c_str(), (unsigned long long)d.offset);
    }
}

// Host tests for the DMC3 text-family decoders (stage `.txt` and `.index`).
//
// Fixtures follow the shapes the structural authority is tested with in
// dmc-rengine-cpp: `src/formats/stage_txt.cpp` for the stage lexer and
// `LooseContainerListPolicy` / `ResourceClassifier` for the manifest grammar.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "dmcresource/text_decode.h"

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) {
        std::cout << "  ok   " << what << "\n";
    } else {
        std::cout << "  FAIL " << what << "\n";
        ++failures;
    }
}

dmcresource::DecodeResult lex(const std::string& text) {
    return dmcresource::decode_stage_txt(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

dmcresource::DecodeResult manifest(const std::string& text) {
    return dmcresource::decode_index(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

bool has_note(const dmcresource::DecodeResult& r) {
    return r.info.find('[') != std::string::npos;
}

bool contains(const dmcresource::DecodeResult& r, const char* needle) {
    return r.info.find(needle) != std::string::npos;
}

// ------------------------------------------------------------- stage `.txt`

void test_stage_tokens() {
    std::cout << "stage text tokens\n";
    const auto r = lex(
        "// leading comment\r\n"
        "#SET room01\r\n"
        "DOOR 1 2.5 -3\r\n"
        "BOXIN \"label\"\r\n"
        "NEXTROOM st445\r\n"
        "STAY\r\n");
    check(r.status == dmcresource::DecodeStatus::Ok, "decodes");
    check(r.format == dmcresource::Format::StageTxt, "reports STAGE-TXT");
    check(r.mesh.vertices.empty(), "produces no geometry");
    check(!r.text.empty(), "carries the text through");
    check(contains(r, "set=1"), "counts the #SET directive");
    check(contains(r, "door=1"), "counts DOOR");
    check(contains(r, "boxin=1"), "counts BOXIN");
    check(contains(r, "nextroom=1"), "counts NEXTROOM");
    check(contains(r, "stageSet=1"), "counts the stage-set value STAY");
    check(contains(r, "str=1"), "counts the quoted string");
    check(contains(r, "num=3"), "counts the three numbers");
    check(!has_note(r), "reports no defects");
}

void test_stage_keywords_are_case_insensitive() {
    std::cout << "stage keywords are case-insensitive\n";
    const auto r = lex("#set a\ndoor b\nBoxIn c\nnextroom d\n");
    check(contains(r, "set=1"), "#set");
    check(contains(r, "door=1"), "door");
    check(contains(r, "boxin=1"), "BoxIn");
    check(contains(r, "nextroom=1"), "nextroom");
}

void test_stage_block_comment_skipped() {
    std::cout << "stage block comment\n";
    const auto r = lex("/* DOOR DOOR */\nDOOR x\n");
    check(r.status == dmcresource::DecodeStatus::Ok, "decodes");
    check(contains(r, "door=1"), "commented keywords are not counted");
    check(!has_note(r), "closed comment is not a defect");
}

void test_stage_reports_unterminated_comment() {
    std::cout << "unterminated block comment\n";
    const auto r = lex("DOOR x\n/* open forever\n");
    check(r.status == dmcresource::DecodeStatus::Ok, "tokens still reported");
    check(has_note(r), "defect reported");
}

void test_stage_reports_unterminated_string() {
    std::cout << "unterminated string\n";
    const auto r = lex("DOOR \"open\n");
    check(has_note(r), "defect reported");
}

void test_stage_rejects_nul_bytes() {
    std::cout << "NUL bytes in a .txt\n";
    std::string binary("DOOR x");
    binary.push_back('\0');
    binary.append("more");
    const auto r = lex(binary);
    check(r.status == dmcresource::DecodeStatus::InvalidData, "rejected as non-text");
}

void test_stage_rejects_empty() {
    std::cout << "empty .txt\n";
    check(lex("").status == dmcresource::DecodeStatus::InvalidData, "rejected");
}

void test_stage_rejects_tokenless() {
    std::cout << "whitespace-only .txt\n";
    check(lex("   \r\n\t\n").status == dmcresource::DecodeStatus::InvalidData, "rejected");
}

// ----------------------------------------------------------------- `.index`

void test_index_pnst_text_line() {
    std::cout << "index manifest with literal PNST line\n";
    // Exactly the shape the authority's classifier test uses: a text manifest
    // whose first line is the literal "PNST", which must never be promoted to
    // a binary PNST container.
    const auto r = manifest("PNST\r\nem035_057_000.txt\r\n");
    check(r.status == dmcresource::DecodeStatus::Ok, "decodes");
    check(r.format == dmcresource::Format::Index, "reports INDEX");
    check(r.mesh.vertices.empty(), "produces no geometry");
    check(contains(r, "directive=PNST"), "records the textual directive");
    check(contains(r, "entries=1"), "counts the entry, not the directive");
    check(contains(r, "not a container"), "states the metadata boundary");
}

void test_index_list_grammar() {
    std::cout << "index list grammar\n";
    const auto r = manifest(
        "/comment\r\n"
        "#PNST\r\n"
        "\r\n"
        "dummy\r\n"
        "child.bin\r\n");
    check(r.status == dmcresource::DecodeStatus::Ok, "decodes");
    check(contains(r, "entries=2"), "blank and comment lines are not entries");
    check(contains(r, "dummy=1"), "counts the declared sparse slot");
    check(contains(r, "comments=1"), "counts the comment line");
    check(contains(r, "directive=#PNST"), "records the # directive");
}

void test_index_without_directive() {
    std::cout << "index manifest without a directive\n";
    const auto r = manifest("first.pac\nsecond.pac\n");
    check(r.status == dmcresource::DecodeStatus::Ok, "decodes");
    check(contains(r, "entries=2"), "counts both entries");
    check(contains(r, "directive=none"), "reports no directive");
}

void test_index_lf_only() {
    std::cout << "index manifest with LF line endings\n";
    const auto r = manifest("PNST\nonly.txt\n");
    check(contains(r, "entries=1"), "LF-only lines parse");
    check(contains(r, "directive=PNST"), "directive still recognised");
}

void test_index_rejects_binary() {
    std::cout << "binary payload under .index\n";
    std::string binary("PNST");
    binary.push_back('\0');
    binary.push_back('\1');
    const auto r = manifest(binary);
    check(r.status == dmcresource::DecodeStatus::InvalidData, "rejected as non-text");
}

void test_index_rejects_empty() {
    std::cout << "empty .index\n";
    check(manifest("").status == dmcresource::DecodeStatus::InvalidData, "rejected");
}

}  // namespace

int main() {
    test_stage_tokens();
    test_stage_keywords_are_case_insensitive();
    test_stage_block_comment_skipped();
    test_stage_reports_unterminated_comment();
    test_stage_reports_unterminated_string();
    test_stage_rejects_nul_bytes();
    test_stage_rejects_empty();
    test_stage_rejects_tokenless();

    test_index_pnst_text_line();
    test_index_list_grammar();
    test_index_without_directive();
    test_index_lf_only();
    test_index_rejects_binary();
    test_index_rejects_empty();

    if (failures != 0) {
        std::cout << "\n" << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nall text decoder checks passed\n";
    return 0;
}

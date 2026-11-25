// Text for a shell is UTF-8 or it is made so: Android's JNI stops the app
// on anything else.
#include "dmcresource/text_utf8.h"

#include <cassert>
#include <string>

namespace text = dmcresource::text;

static_assert(text::is_utf8("plain.mod"));
static_assert(text::is_utf8("\xD0\xB0\xD0\xBD\xD1\x96"));          // Cyrillic
static_assert(text::is_utf8("\xE6\xAD\xA9\xE3\x81\x8F"));          // Japanese
static_assert(text::is_utf8("\xF0\x9F\x8E\xA5"));                  // a 4-byte character
static_assert(!text::is_utf8("\xD0"));                             // cut in half
static_assert(!text::is_utf8("a\x80z"));                           // a stray continuation
static_assert(!text::is_utf8("\xC0\xAF"));                         // overlong
static_assert(!text::is_utf8("\xED\xA0\x80"));                     // a surrogate
static_assert(!text::is_utf8("\xF4\x90\x80\x80"));                 // past U+10FFFF
static_assert(text::utf8_character("\xE6\xAD\xA9", 0) == 3U);
static_assert(text::utf8_character("abc", 5) == 0U);

int main() {
    assert(text::as_utf8("plain.mod") == "plain.mod");
    assert(text::as_utf8("\xD0\xB0\xD0") == "\xD0\xB0?");
    assert(text::as_utf8("a\xFF\xFEz") == "a??z");
    assert(text::as_utf8("\xE6\xAD") == "??");
    assert(text::is_utf8(text::as_utf8(std::string("\x80\x81\xE6\xAD\xA9\xF8", 6))));
    return 0;
}

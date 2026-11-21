# stb_vorbis

`stb_vorbis.c` v1.22 by Sean Barrett, public domain (or MIT, the reader's
choice; the licence text is at the end of the file), from
https://github.com/nothings/stb at commit
`f58f558c120e9b32c217290b80bad1a0729fbb2c`, unmodified.

The Reader decodes Ogg Vorbis with it (`sound::decode_ogg`): the HD release
stores the game's ADX streams as OGG. It is built as its own C library
(`dmc_native_reader_stb_vorbis`) with warnings off; only
`stb_vorbis_decode_memory` is used.

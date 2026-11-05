# FFmpeg (minimal, fetched at build time)

The Reader decodes the HD release's movies (`.wmv`: ASF with Windows Media
video and audio) with FFmpeg 7.1.1, LGPL 2.1 or later. Its source is not
kept here: `CMakeLists.txt` fetches the official release tarball

    https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz
    SHA256 733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1

(signed by the FFmpeg release signing key, fingerprint
`FCF9 86EA 15E6 E293 A564 4F10 B432 2F04 D676 58D8`; the hash pins that
tarball), unmodified, and builds it with `ExternalProject` as static
libraries with only what movies need:

    --disable-everything --disable-programs --disable-doc --disable-network
    --disable-autodetect --disable-avdevice --disable-swscale
    --disable-swresample --disable-postproc --disable-avfilter --disable-debug
    --enable-static --disable-shared --enable-pic
    --enable-demuxer=asf
    --enable-decoder=wmv1,wmv2,wmv3,vc1,msmpeg4v3,wmav1,wmav2,wmapro
    --enable-parser=vc1

(Android: cross-compiled with the NDK's clang for the app's ABI.) Only
`libavformat`, `libavcodec` and `libavutil` are linked; `modules/video.cpp`
reads the file from memory through a custom AVIO, converts the YUV 4:2:0
pictures to RGBA itself (BT.601) and the sound to 16-bit PCM.

Building needs `make` and a POSIX shell (Linux, macOS, the CI). Configure with
`-DDMC_NATIVE_READER_FFMPEG=OFF` to build without it (MSVC builds always do):
the Reader then reads a movie's identity and says it cannot play it.

FFmpeg is a trademark of Fabrice Bellard. Its licence:
https://ffmpeg.org/legal.html; the LGPL text ships in the tarball
(`COPYING.LGPLv2.1`).

#import "DmcBridge.h"

#include <algorithm>
#include <memory>
#include <string>

#include "dmcresource/decode.h"
#include "dmcresource/view_renderer.h"

NSString *const DmcResourceErrorDomain = @"com.dmcrengine.nativereader";

namespace {

/// Mirrors the Android JNI cap so both platforms refuse the same inputs.
constexpr NSUInteger kMaxResourceBytes = 512u * 1024u * 1024u;

NSError *MakeError(NSInteger code, NSString *message) {
    return [NSError errorWithDomain:DmcResourceErrorDomain
                               code:code
                           userInfo:@{NSLocalizedDescriptionKey : message}];
}

/// RGBA8888 from the shared renderer into a UIImage.
UIImage *ImageFromRgba(const dmcresource::RgbaImage &image) {
    if (image.width <= 0 || image.height <= 0) return nil;
    const size_t expected =
        static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4u;
    if (image.pixels.size() != expected) return nil;

    NSData *data = [NSData dataWithBytes:image.pixels.data() length:expected];
    CGDataProviderRef provider =
        CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
    if (provider == nullptr) return nil;

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGImageRef cgImage = CGImageCreate(
        static_cast<size_t>(image.width), static_cast<size_t>(image.height),
        8, 32, static_cast<size_t>(image.width) * 4u, space,
        kCGBitmapByteOrderDefault | kCGImageAlphaPremultipliedLast, provider,
        nullptr, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    if (cgImage == nullptr) return nil;

    UIImage *result = [UIImage imageWithCGImage:cgImage];
    CGImageRelease(cgImage);
    return result;
}

}  // namespace

@implementation DmcResource {
    // Renamed away from the property names on purpose: an ObjC readonly
    // property would otherwise try to synthesize an ivar called `_text`,
    // colliding with this C++ member.
    dmcresource::Mesh _meshData;
    std::string _textData;
}

- (nullable instancetype)initWithContentsOfURL:(NSURL *)url
                                         error:(NSError **)error {
    self = [super init];
    if (self == nil) return nil;

    // A picked document is security-scoped; without this the read fails with a
    // permission error even though the user chose the file.
    const BOOL scoped = [url startAccessingSecurityScopedResource];
    @try {
        NSError *readError = nil;
        // Mapped-if-safe mirrors the Android mmap path: the decoder never
        // needs a writable copy of the resource.
        NSData *data = [NSData dataWithContentsOfURL:url
                                             options:NSDataReadingMappedIfSafe
                                               error:&readError];
        if (data == nil) {
            if (error != nullptr) {
                *error = readError != nil ? readError
                                          : MakeError(1, @"Could not read the file");
            }
            return nil;
        }
        if (data.length > kMaxResourceBytes) {
            if (error != nullptr) {
                *error = MakeError(2, @"Resource exceeds the decoder cap");
            }
            return nil;
        }

        const char *rawName = url.lastPathComponent.UTF8String;
        const std::string name(rawName != nullptr ? rawName : "resource.bin");
        auto decoded = dmcresource::decode_resource(
            name, static_cast<const std::uint8_t *>(data.bytes), data.length);
        if (decoded.status != dmcresource::DecodeStatus::Ok) {
            if (error != nullptr) {
                NSString *detail = decoded.detail != nullptr
                                       ? @(decoded.detail)
                                       : @"decoder rejected this file";
                *error = MakeError(3, [NSString stringWithFormat:@"%s: %@",
                                       dmcresource::decode_status_name(decoded.status),
                                       detail]);
            }
            return nil;
        }

        _meshData = std::move(decoded.mesh);
        _textData = std::move(decoded.text);
        _formatName = @(dmcresource::format_name(decoded.format));

        NSMutableString *summary = [NSMutableString string];
        [summary appendString:_formatName];
        if (_textData.empty()) {
            [summary appendFormat:@" | vertices=%lu | triangles=%lu",
                                  (unsigned long)_meshData.vertices.size(),
                                  (unsigned long)(_meshData.indices.size() / 3u)];
        } else {
            [summary appendFormat:@" | bytes=%lu", (unsigned long)_textData.size()];
        }
        if (decoded.detail != nullptr) {
            [summary appendFormat:@" | %s", decoded.detail];
        }
        if (!decoded.info.empty()) {
            [summary appendFormat:@" | %s", decoded.info.c_str()];
        }
        _summary = [summary copy];
        return self;
    } @finally {
        if (scoped) [url stopAccessingSecurityScopedResource];
    }
}

- (nullable NSString *)text {
    if (_textData.empty()) return nil;
    // Stage texts are not guaranteed UTF-8; fall back to Latin-1 so a resource
    // with high bytes still displays instead of vanishing.
    NSString *utf8 = [[NSString alloc] initWithBytes:_textData.data()
                                              length:_textData.size()
                                            encoding:NSUTF8StringEncoding];
    if (utf8 != nil) return utf8;
    return [[NSString alloc] initWithBytes:_textData.data()
                                    length:_textData.size()
                                  encoding:NSISOLatin1StringEncoding];
}

- (BOOL)isText {
    return !_textData.empty();
}

- (NSUInteger)vertexCount {
    return _meshData.vertices.size();
}

- (NSUInteger)triangleCount {
    return _meshData.indices.size() / 3u;
}

- (nullable UIImage *)renderWithSize:(CGSize)size
                                 yaw:(float)yaw
                               pitch:(float)pitch
                                zoom:(float)zoom
                           wireframe:(BOOL)wireframe {
    if (!_textData.empty() || _meshData.vertices.empty()) return nil;

    dmcresource::ViewState view;
    view.yaw_radians = yaw;
    view.pitch_radians = std::clamp(pitch, -1.55f, 1.55f);
    view.zoom = std::clamp(zoom, 0.15f, 8.0f);
    view.wireframe = wireframe == YES;

    // Same clamp as the Android bridge, so both platforms render identically.
    const int width = std::clamp(static_cast<int>(size.width), 64, 1024);
    const int height = std::clamp(static_cast<int>(size.height), 64, 1024);
    return ImageFromRgba(dmcresource::render_view(_meshData, width, height, view));
}

@end

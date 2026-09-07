#import "DmcBridge.h"

#include <algorithm>
#include <memory>
#include <string>

#include "dmcresource/portable_session.h"

NSString *const DmcResourceErrorDomain = @"com.dmcrengine.nativereader";

namespace {
constexpr NSUInteger kMaxResourceBytes = 512u * 1024u * 1024u;

NSError *MakeError(NSInteger code, NSString *message) {
    return [NSError errorWithDomain:DmcResourceErrorDomain
                               code:code
                           userInfo:@{NSLocalizedDescriptionKey : message}];
}

UIImage *ImageFromRgba(const std::uint8_t *pixels,
                       std::size_t byteCount,
                       std::uint32_t width,
                       std::uint32_t height) {
    if (pixels == nullptr || width == 0U || height == 0U) return nil;
    const std::size_t expected = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height) * 4U;
    if (byteCount != expected) return nil;

    NSData *data = [NSData dataWithBytes:pixels length:expected];
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
    if (provider == nullptr) return nil;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGImageRef cgImage = CGImageCreate(width,
                                      height,
                                      8,
                                      32,
                                      static_cast<std::size_t>(width) * 4U,
                                      space,
                                      kCGBitmapByteOrderDefault | kCGImageAlphaLast,
                                      provider,
                                      nullptr,
                                      false,
                                      kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    if (cgImage == nullptr) return nil;
    UIImage *image = [UIImage imageWithCGImage:cgImage];
    CGImageRelease(cgImage);
    return image;
}

UIImage *ImageFromPreview(const dmcresource::ImagePreview &preview) {
    if (!preview.available()) return nil;
    return ImageFromRgba(preview.rgba8.data(), preview.rgba8.size(),
                         preview.width, preview.height);
}

UIImage *ImageFromRender(const dmcresource::RgbaImage &image) {
    if (image.width <= 0 || image.height <= 0) return nil;
    return ImageFromRgba(image.pixels.data(), image.pixels.size(),
                         static_cast<std::uint32_t>(image.width),
                         static_cast<std::uint32_t>(image.height));
}
}  // namespace

@implementation DmcResource {
    std::unique_ptr<dmcresource::PortableSession> _session;
}

- (nullable instancetype)initWithContentsOfURL:(NSURL *)url
                                         error:(NSError **)error {
    self = [super init];
    if (self == nil) return nil;

    const BOOL scoped = [url startAccessingSecurityScopedResource];
    @try {
        NSError *readError = nil;
        NSData *data = [NSData dataWithContentsOfURL:url
                                             options:NSDataReadingMappedIfSafe
                                               error:&readError];
        if (data == nil) {
            if (error != nullptr) {
                *error = readError ?: MakeError(1, @"Could not read the selected file");
            }
            return nil;
        }
        if (data.length == 0U || data.length > kMaxResourceBytes) {
            if (error != nullptr) {
                *error = MakeError(2, @"Resource is empty or exceeds the 512 MiB safety cap");
            }
            return nil;
        }

        const char *rawName = url.lastPathComponent.UTF8String;
        std::string nativeError;
        _session = dmcresource::PortableSession::decode(
            rawName != nullptr ? rawName : "resource.bin",
            static_cast<const std::uint8_t *>(data.bytes),
            data.length,
            &nativeError);
        if (_session == nullptr) {
            if (error != nullptr) {
                NSString *message = nativeError.empty()
                    ? @"Architecture v2 rejected this resource"
                    : [NSString stringWithUTF8String:nativeError.c_str()];
                *error = MakeError(3, message);
            }
            return nil;
        }

        const auto &result = _session->result();
        const char *family = result.probe.family != nullptr ? result.probe.family : "UNKNOWN";
        _formatName = [NSString stringWithUTF8String:family];
        const std::string summary = _session->summary();
        _summary = [NSString stringWithUTF8String:summary.c_str()];
        const std::string inspection = _session->inspection_text();
        _inspectionText = [NSString stringWithUTF8String:inspection.c_str()];
        return self;
    } @finally {
        if (scoped) [url stopAccessingSecurityScopedResource];
    }
}

- (BOOL)hasGeometry {
    return _session != nullptr && _session->has_geometry();
}

- (nullable UIImage *)imagePreview {
    if (_session == nullptr) return nil;
    return ImageFromPreview(_session->result().image_preview);
}

- (NSUInteger)childCount {
    return _session != nullptr ? _session->child_count() : 0U;
}

- (nullable NSString *)childTitleAtIndex:(NSUInteger)index {
    if (_session == nullptr) return nil;
    const auto *child = _session->child(index);
    if (child == nullptr) return nil;
    return [NSString stringWithUTF8String:child->title.c_str()];
}

- (nullable UIImage *)childImageAtIndex:(NSUInteger)index {
    if (_session == nullptr) return nil;
    const auto *child = _session->child(index);
    if (child == nullptr) return nil;
    return ImageFromPreview(child->image_preview);
}

- (nullable UIImage *)renderWithSize:(CGSize)size
                                 yaw:(float)yaw
                               pitch:(float)pitch
                                zoom:(float)zoom
                           wireframe:(BOOL)wireframe {
    if (_session == nullptr || !_session->has_geometry()) return nil;
    dmcresource::ViewState view;
    view.yaw_radians = yaw;
    view.pitch_radians = std::clamp(pitch, -1.55F, 1.55F);
    view.zoom = std::clamp(zoom, 0.15F, 8.0F);
    view.wireframe = wireframe == YES;
    const int width = std::clamp(static_cast<int>(size.width), 64, 2048);
    const int height = std::clamp(static_cast<int>(size.height), 64, 2048);
    return ImageFromRender(_session->render(width, height, view));
}

@end

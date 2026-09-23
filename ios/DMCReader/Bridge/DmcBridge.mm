#import "DmcBridge.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <memory>
#include <string>

#include "reader_bridge.h"

static NSString *const DmcResourceErrorDomain = @"com.dmcrengine.nativereader";

namespace {

NSError *MakeError(NSInteger code, NSString *message) {
    return [NSError errorWithDomain:DmcResourceErrorDomain
                               code:code
                           userInfo:@{NSLocalizedDescriptionKey : message}];
}

// Native strings are not guaranteed to be UTF-8. Never let a conversion
// return nil and take the UI down with it; fall back to Latin-1, which
// accepts every byte sequence.
NSString *ToNSString(const std::string &value) {
    if (value.empty()) return @"";
    NSString *text = [[NSString alloc] initWithBytes:value.data()
                                              length:value.size()
                                            encoding:NSUTF8StringEncoding];
    if (text == nil) {
        text = [[NSString alloc] initWithBytes:value.data()
                                        length:value.size()
                                      encoding:NSISOLatin1StringEncoding];
    }
    return text ?: @"";
}

std::string ToStdString(NSString *value) {
    const char *raw = value.UTF8String;
    return raw != nullptr ? std::string(raw) : std::string();
}

// Pixels are straight (non-premultiplied) RGBA8888: texture previews can carry
// real alpha, so they must not be declared premultiplied.
UIImage *ImageFromPixels(const dmc_ios::Pixels &pixels) {
    if (pixels.width == 0U || pixels.height == 0U) return nil;
    const size_t row_bytes = static_cast<size_t>(pixels.width) * 4U;
    if (pixels.rgba.size() != row_bytes * pixels.height) return nil;

    NSData *data = [NSData dataWithBytes:pixels.rgba.data() length:pixels.rgba.size()];
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
    if (provider == nullptr) return nil;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGImageRef image = CGImageCreate(pixels.width, pixels.height, 8, 32, row_bytes, space,
                                     kCGBitmapByteOrderDefault | kCGImageAlphaLast,
                                     provider, nullptr, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    if (image == nullptr) return nil;
    UIImage *result = [UIImage imageWithCGImage:image];
    CGImageRelease(image);
    return result;
}

// Reads a user-picked file. Picked documents are security-scoped; the read
// fails with a permission error unless access is started first.
NSData *ReadDocument(NSURL *url, NSError **error) {
    const BOOL scoped = [url startAccessingSecurityScopedResource];
    NSError *readError = nil;
    NSData *data = [NSData dataWithContentsOfURL:url
                                         options:NSDataReadingMappedIfSafe
                                           error:&readError];
    if (scoped) [url stopAccessingSecurityScopedResource];
    if (data == nil && error != nullptr) {
        *error = readError ?: MakeError(1, @"Could not read the file");
    }
    return data;
}

bool Has(std::uint64_t bits, std::uint64_t flag) { return (bits & flag) != 0U; }

}  // namespace

@interface DmcResource ()
// Takes ownership of `session`, including on failure.
- (instancetype)initWithOwnedSession:(dmc_ios::ReaderSession *)session
                               title:(NSString *)title;
@end

@implementation DmcResource {
    std::unique_ptr<dmc_ios::ReaderSession> _session;
}

- (instancetype)initWithOwnedSession:(dmc_ios::ReaderSession *)session
                               title:(NSString *)title {
    self = [super init];
    if (self == nil) {
        delete session;
        return nil;
    }
    _session.reset(session);
    _title = [title copy];
    return self;
}

- (nullable instancetype)initWithContentsOfURL:(NSURL *)url error:(NSError **)error {
    NSData *data = ReadDocument(url, error);
    if (data == nil) return nil;

    NSString *name = url.lastPathComponent ?: @"resource.bin";
    auto session = dmc_ios::ReaderSession::open(
        ToStdString(name), static_cast<const std::uint8_t *>(data.bytes), data.length);
    if (!session) {
        if (error != nullptr) {
            *error = MakeError(2, @"Unsupported or malformed DMC resource. Supported: "
                                  @"MOD, SCM, DDS, PTX, EventTbl, TM2.");
        }
        return nil;
    }
    return [self initWithOwnedSession:session.release() title:name];
}

- (NSString *)summary {
    return ToNSString(_session->describe());
}

- (NSString *)inspection {
    return ToNSString(_session->inspection());
}

- (BOOL)canRender {
    return Has(_session->state(), dmc_ios::state::kCanRender);
}

- (BOOL)canWireframe {
    return Has(_session->state(), dmc_ios::state::kCanWireframe);
}

- (BOOL)canPreviewImage {
    return Has(_session->state(), dmc_ios::state::kCanPreviewImage);
}

- (BOOL)isChildBrowser {
    return Has(_session->state(), dmc_ios::state::kChildBrowserMode);
}

- (BOOL)canAttachTexture {
    return Has(_session->state(), dmc_ios::state::kTextureCompanionAttachable);
}

- (BOOL)textureAttached {
    return Has(_session->state(), dmc_ios::state::kTextureCompanionAttached);
}

- (nullable UIImage *)renderWithSize:(CGSize)size
                                 yaw:(float)yaw
                               pitch:(float)pitch
                                zoom:(float)zoom
                           wireframe:(BOOL)wireframe {
    if (!(size.width > 0.0) || !(size.height > 0.0)) return nil;
    // Keep the aspect ratio while bounding the CPU render to 1024 px.
    const double scale = std::min(1.0, 1024.0 / std::max(size.width, size.height));
    const int width = std::max(64, static_cast<int>(std::lround(size.width * scale)));
    const int height = std::max(64, static_cast<int>(std::lround(size.height * scale)));

    dmc_ios::Pixels pixels;
    const std::uint32_t flags = wireframe ? dmc_ios::render_flag::kWireframe : 0U;
    if (!_session->render(width, height, yaw, pitch, zoom, flags, &pixels)) return nil;
    return ImageFromPixels(pixels);
}

- (nullable UIImage *)imagePreview {
    dmc_ios::Pixels pixels;
    if (!_session->image_preview(&pixels)) return nil;
    return ImageFromPixels(pixels);
}

- (NSInteger)childCount {
    const auto count = _session->child_count();
    return count > static_cast<std::size_t>(NSIntegerMax) ? 0 : static_cast<NSInteger>(count);
}

- (NSString *)childTitleAtIndex:(NSInteger)index {
    if (index < 0 || index > INT_MAX) return @"";
    return ToNSString(_session->child_title(static_cast<int>(index)));
}

- (nullable UIImage *)childPreviewAtIndex:(NSInteger)index {
    if (index < 0 || index > INT_MAX) return nil;
    dmc_ios::Pixels pixels;
    if (!_session->child_preview(static_cast<int>(index), &pixels)) return nil;
    return ImageFromPixels(pixels);
}

- (nullable DmcResource *)openChildAtIndex:(NSInteger)index {
    if (index < 0 || index > INT_MAX) return nil;
    auto child = _session->open_child(static_cast<int>(index));
    if (!child) return nil;
    return [[DmcResource alloc] initWithOwnedSession:child.release()
                                               title:[self childTitleAtIndex:index]];
}

- (BOOL)attachTextureFromURL:(NSURL *)url error:(NSError **)error {
    NSData *data = ReadDocument(url, error);
    if (data == nil) return NO;
    NSString *name = url.lastPathComponent ?: @"texture.ptx";
    const bool attached = _session->attach_texture(
        ToStdString(name), static_cast<const std::uint8_t *>(data.bytes), data.length);
    if (!attached && error != nullptr) {
        NSString *detail = self.textureAttachmentDetail;
        *error = MakeError(3, detail.length > 0 ? detail : @"Texture companion rejected");
    }
    return attached ? YES : NO;
}

- (NSString *)textureAttachmentDetail {
    return ToNSString(_session->texture_attachment_detail());
}

@end

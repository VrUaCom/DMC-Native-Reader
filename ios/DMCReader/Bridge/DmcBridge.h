#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

/// Objective-C facade over the shared `dmcresource` C++ decoder.
///
/// The decoder, the probe and the CPU renderer are the same translation units
/// the Android app compiles: only the JNI entry point is platform-specific, so
/// iOS reuses `app/src/main/cpp` directly rather than forking a second parser.
@interface DmcResource : NSObject

/// Format name as reported by the decoder: SCM, MOD, HITS, STAGE-TXT, INDEX.
@property(nonatomic, readonly) NSString *formatName;

/// One-line structural summary shown in the status area.
@property(nonatomic, readonly) NSString *summary;

/// Decoded text for a text-family resource; nil for geometry formats.
@property(nonatomic, readonly, nullable) NSString *text;

/// YES when this resource is text and must be shown in the text view.
@property(nonatomic, readonly) BOOL isText;

@property(nonatomic, readonly) NSUInteger vertexCount;
@property(nonatomic, readonly) NSUInteger triangleCount;

/// Decodes the file at `url`. Returns nil and populates `error` when the
/// native probe or decoder rejects the bytes.
///
/// Declared as an initialiser rather than a factory method so Swift imports it
/// by the standard NSError convention as `try DmcResource(contentsOf: url)`.
- (nullable instancetype)initWithContentsOfURL:(NSURL *)url
                                         error:(NSError **)error;

- (instancetype)init NS_UNAVAILABLE;

/// Renders the mesh with the CPU renderer. Returns nil for text resources.
- (nullable UIImage *)renderWithSize:(CGSize)size
                                 yaw:(float)yaw
                               pitch:(float)pitch
                                zoom:(float)zoom
                           wireframe:(BOOL)wireframe;

@end

NS_ASSUME_NONNULL_END

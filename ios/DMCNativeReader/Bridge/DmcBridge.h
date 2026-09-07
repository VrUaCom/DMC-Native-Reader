#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

FOUNDATION_EXPORT NSString *const DmcResourceErrorDomain;

// Keep these bit values aligned with dmcresource::RenderFlag. The platform
// bridge transports generic rendering policy only; it does not know formats.
typedef NS_OPTIONS(NSUInteger, DmcRenderFlags) {
    DmcRenderFlagWireframe = 1U << 0U,
    DmcRenderFlagHierarchy = 1U << 1U,
};

// Objective-C++ facade over the shared C++20 Architecture v2 session.
// No format parser lives in Swift/Objective-C.
@interface DmcResource : NSObject

@property(nonatomic, readonly) NSString *formatName;
@property(nonatomic, readonly) NSString *summary;
@property(nonatomic, readonly) NSString *inspectionText;
@property(nonatomic, readonly) BOOL hasGeometry;
@property(nonatomic, readonly) BOOL hierarchyAvailable;
@property(nonatomic, readonly, nullable) UIImage *imagePreview;
@property(nonatomic, readonly) NSUInteger childCount;

- (nullable instancetype)initWithContentsOfURL:(NSURL *)url
                                         error:(NSError **)error;
- (instancetype)init NS_UNAVAILABLE;

- (nullable NSString *)childTitleAtIndex:(NSUInteger)index NS_SWIFT_NAME(childTitle(at:));
- (nullable UIImage *)childImageAtIndex:(NSUInteger)index NS_SWIFT_NAME(childImage(at:));

- (nullable UIImage *)renderWithSize:(CGSize)size
                                 yaw:(float)yaw
                               pitch:(float)pitch
                                zoom:(float)zoom
                               flags:(DmcRenderFlags)flags;

@end

NS_ASSUME_NONNULL_END

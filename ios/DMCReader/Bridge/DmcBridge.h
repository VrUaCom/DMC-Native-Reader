#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

/// Objective-C view of one DMCNativeReader::Core session.
///
/// A type-conversion layer only: every decision (format, capabilities,
/// composition, texture binding) is made natively and read through
/// reader_bridge.h. Capability properties reflect Spider Black Widow state and
/// are re-read on every access, because attaching a texture changes it.
@interface DmcResource : NSObject

/// Opens and decodes the file at `url`. Fails with an NSError when the native
/// probe or decoder rejects the bytes.
- (nullable instancetype)initWithContentsOfURL:(NSURL *)url
                                         error:(NSError **)error;
- (instancetype)init NS_UNAVAILABLE;

@property(nonatomic, readonly) NSString *title;

/// One-paragraph session description from the Core (`describe_session`).
@property(nonatomic, readonly) NSString *summary;
/// Typed inspection tree, formatted by the Core.
@property(nonatomic, readonly) NSString *inspection;

@property(nonatomic, readonly) BOOL canRender;
@property(nonatomic, readonly) BOOL canWireframe;
@property(nonatomic, readonly) BOOL canPreviewImage;
@property(nonatomic, readonly) BOOL isChildBrowser;
@property(nonatomic, readonly) BOOL canAttachTexture;
@property(nonatomic, readonly) BOOL textureAttached;

/// CPU render of a renderable session; nil otherwise. The size is clamped the
/// same way on every platform shell.
- (nullable UIImage *)renderWithSize:(CGSize)size
                                 yaw:(float)yaw
                               pitch:(float)pitch
                                zoom:(float)zoom
                           wireframe:(BOOL)wireframe;

/// Decoded image of an image resource (DDS, TM2); nil otherwise.
- (nullable UIImage *)imagePreview;

@property(nonatomic, readonly) NSInteger childCount;
- (NSString *)childTitleAtIndex:(NSInteger)index;
- (nullable UIImage *)childPreviewAtIndex:(NSInteger)index;
- (nullable DmcResource *)openChildAtIndex:(NSInteger)index;

/// Binds a PTX/DDS texture companion to this model. On rejection the error
/// carries the Core's reason (`textureAttachmentDetail`).
- (BOOL)attachTextureFromURL:(NSURL *)url error:(NSError **)error;
@property(nonatomic, readonly) NSString *textureAttachmentDetail;

@end

NS_ASSUME_NONNULL_END

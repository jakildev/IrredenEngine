#include <irreden/render/metal/metal_cocoa_bridge.hpp>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

namespace IRRender {

void *createMetalLayerForWindow(void *glfwWindow, void *mtlDevice) {
    NSWindow *nsWindow = glfwGetCocoaWindow(static_cast<GLFWwindow *>(glfwWindow));
    NSView *contentView = [nsWindow contentView];
    [contentView setWantsLayer:YES];

    CAMetalLayer *metalLayer = [CAMetalLayer layer];
    metalLayer.device = (__bridge id<MTLDevice>)mtlDevice;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.framebufferOnly = NO;

    CGSize viewSize = [contentView bounds].size;
    CGFloat scaleFactor = [nsWindow backingScaleFactor];
    metalLayer.contentsScale = scaleFactor;
    metalLayer.drawableSize = CGSizeMake(viewSize.width * scaleFactor,
                                         viewSize.height * scaleFactor);

    [contentView setLayer:metalLayer];
    return (__bridge void *)metalLayer;
}

void resizeMetalLayer(void *layerPtr, int widthPixels, int heightPixels) {
    CAMetalLayer *layer = (__bridge CAMetalLayer *)layerPtr;
    layer.drawableSize = CGSizeMake(widthPixels, heightPixels);
}

} // namespace IRRender

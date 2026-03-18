#pragma once

namespace IRRender {

// Creates a CAMetalLayer, attaches it to the GLFW window's NSView,
// and returns the layer as an opaque pointer (bridges to CA::MetalLayer*).
// glfwWindow: raw GLFWwindow*
// mtlDevice:  MTL::Device* cast to void*
void *createMetalLayerForWindow(void *glfwWindow, void *mtlDevice);

// Resizes the CAMetalLayer's drawableSize (in backing pixels).
void resizeMetalLayer(void *layerPtr, int widthPixels, int heightPixels);

} // namespace IRRender

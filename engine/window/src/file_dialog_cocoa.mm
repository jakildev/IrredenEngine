// macOS half of file_dialog.hpp (Windows: file_dialog_win32.cpp, Linux:
// file_dialog_linux.cpp).
#include <irreden/window/file_dialog.hpp>

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

namespace IRWindow {

namespace {

NSString *toNSString(const std::string &text) {
    return [[[NSString alloc] initWithBytes:text.data()
                                     length:text.size()
                                   encoding:NSUTF8StringEncoding] autorelease];
}

// The union of every filter's extensions: NSOpenPanel has no filter menu, so
// the filters can only narrow what is selectable. nil (no restriction) when a
// filter matches everything or an extension has no uniform type.
NSArray<UTType *> *allowedTypes(const FileDialogRequest &request) {
    NSMutableArray<UTType *> *types = [NSMutableArray array];
    for (const FileDialogFilter &filter : request.filters_) {
        for (const std::string &extension : filter.extensions_) {
            if (extension == "*") {
                return nil;
            }
            UTType *type = [UTType typeWithFilenameExtension:toNSString(extension)];
            if (type == nil) {
                return nil;
            }
            [types addObject:type];
        }
    }
    return [types count] == 0 ? nil : types;
}

// A sheet is window-modal and asynchronous. Pumping the application's own
// event queue until its handler runs keeps the call synchronous while GLFW's
// window delegate stays live, so the engine window still receives the
// focus-loss key releases and its resize / close notifications.
NSModalResponse runSheet(NSOpenPanel *panel, NSWindow *parent) {
    __block NSModalResponse response = NSModalResponseCancel;
    __block bool finished = false;
    [panel beginSheetModalForWindow:parent
                  completionHandler:^(NSModalResponse sheetResponse) {
                    response = sheetResponse;
                    finished = true;
                    // Wakes the blocking dequeue below when the handler ran
                    // outside an event.
                    glfwPostEmptyEvent();
                  }];
    while (!finished) {
        @autoreleasepool {
            NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantFuture]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event != nil) {
                [NSApp sendEvent:event];
            }
        }
    }
    return response;
}

} // namespace

FileDialogResult platformOpenFileDialog(GLFWwindow *window, const FileDialogRequest &request) {
    FileDialogResult result;
    if (![NSThread isMainThread]) {
        result.status_ = FileDialogStatus::FAILED;
        result.error_ = "NSOpenPanel must run on the main thread";
        return result;
    }
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        const bool pickFolder = request.mode_ == FileDialogMode::OPEN_FOLDER;
        [panel setCanChooseFiles:!pickFolder];
        [panel setCanChooseDirectories:pickFolder];
        [panel setAllowsMultipleSelection:request.mode_ == FileDialogMode::OPEN_FILES];
        [panel setCanCreateDirectories:NO];
        [panel setResolvesAliases:YES];
        if (!request.title_.empty()) {
            // A sheet has no title bar; the message line is what stays visible.
            [panel setTitle:toNSString(request.title_)];
            [panel setMessage:toNSString(request.title_)];
        }
        if (!request.directory_.empty()) {
            BOOL isDirectory = NO;
            NSString *directory = toNSString(request.directory_);
            if ([[NSFileManager defaultManager] fileExistsAtPath:directory
                                                     isDirectory:&isDirectory] &&
                isDirectory) {
                [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
            }
        }
        if (NSArray<UTType *> *types = allowedTypes(request)) {
            [panel setAllowedContentTypes:types];
        }

        NSWindow *parent = window == nullptr ? nil : glfwGetCocoaWindow(window);
        const bool canHostSheet = parent != nil && [parent isVisible] && ![parent isMiniaturized];
        const NSModalResponse response = canHostSheet ? runSheet(panel, parent) : [panel runModal];
        if (response != NSModalResponseOK) {
            result.status_ = FileDialogStatus::CANCELLED;
            return result;
        }
        for (NSURL *url in [panel URLs]) {
            if ([url isFileURL] && [url fileSystemRepresentation] != nullptr) {
                result.paths_.emplace_back([url fileSystemRepresentation]);
            }
        }
        result.status_ = FileDialogStatus::ACCEPTED;
    }
    return result;
}

} // namespace IRWindow

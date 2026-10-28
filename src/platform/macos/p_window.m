#import <AppKit/AppKit.h>
#import <QuartzCore/CADisplayLink.h>

#include "o_log.h"
#include "o_window.h"

#include <stdlib.h>
#include <string.h>

static struct {
    bool initialized;
    uint32_t window_count;
} g_app;

struct OWindow {
    void *ns_window;
    OWindowCallbacks callbacks;
    void *user_data;
    bool open;
    bool shown;
};

static NSWindow *ns_window_of(const OWindow *window) {
    return (__bridge NSWindow *)window->ns_window;
}

static void wake_event_loop(void) {
    NSEvent *wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                       location:NSZeroPoint
                                  modifierFlags:0
                                      timestamp:0
                                   windowNumber:0
                                        context:nil
                                        subtype:0
                                          data1:0
                                          data2:0];
    [NSApp postEvent:wake atStart:YES];
}

@interface OMacWindow : NSWindow
@end

@implementation OMacWindow
- (BOOL)canBecomeKeyWindow {
    return YES;
}
@end

@interface OMacView : NSView <NSWindowDelegate>
@property(nonatomic, assign) OWindow *owner;
@property(nonatomic, strong) CADisplayLink *displayLink API_AVAILABLE(macos(14.0));
@end

@implementation OMacView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)onDisplayLink:(CADisplayLink *)sender {
    (void)sender;
    OWindow *w = self.owner;
    if (w && w->callbacks.on_refresh) w->callbacks.on_refresh(w, w->user_data);
}

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    OWindow *w = self.owner;
    if (!w || !w->open) return;
    w->open = false;
    g_app.window_count--;
    if (w->callbacks.on_close) w->callbacks.on_close(w, w->user_data);
    wake_event_loop();
}

- (void)windowDidResize:(NSNotification *)notification {
    (void)notification;
    OWindow *w = self.owner;
    if (!w || !w->callbacks.on_resize) return;
    NSSize size = self.bounds.size;
    w->callbacks.on_resize(w, (int32_t)size.width, (int32_t)size.height, w->user_data);
}

- (void)windowDidMove:(NSNotification *)notification {
    (void)notification;
    OWindow *w = self.owner;
    if (!w || !w->callbacks.on_move) return;
    NSPoint origin = self.window.frame.origin;
    w->callbacks.on_move(w, (int32_t)origin.x, (int32_t)origin.y, w->user_data);
}

// Swallows keys so AppKit does not beep; o_input delivers them.
- (void)keyDown:(NSEvent *)event {
    (void)event;
}

@end

static void app_init(void) {
    if (g_app.initialized) return;

    if (RENDERER_VALIDATION) {
        setenv("MTL_DEBUG_LAYER", "1", 0);
        // setenv("MTL_SHADER_VALIDATION", "1", 0);
        setenv("MTL_DEBUG_LAYER_ERROR_MODE", "assert", 0);
        setenv("MTL_CAPTURE_ENABLED", "1", 0);
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSMenu *menubar = [[NSMenu alloc] init];
    NSMenuItem *app_item = [[NSMenuItem alloc] init];
    NSMenu *app_menu = [[NSMenu alloc] init];
    NSString *quit_title = [@"Quit " stringByAppendingString:NSProcessInfo.processInfo.processName];
    [app_menu addItem:[[NSMenuItem alloc] initWithTitle:quit_title action:@selector(terminate:) keyEquivalent:@"q"]];
    app_item.submenu = app_menu;
    [menubar addItem:app_item];
    NSApp.mainMenu = menubar;

    [NSApp finishLaunching];

    g_app.initialized = true;
}

bool window_poll_events(void) {
    if (!g_app.initialized) return false;

    @autoreleasepool {
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:NSDate.distantFuture
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        while (event) {
            [NSApp sendEvent:event];
            event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:NSDate.distantPast
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES];
        }
    }
    return g_app.window_count > 0;
}

OWindow *window_create(const OWindowDesc *desc) {
    if (!desc || desc->width <= 0 || desc->height <= 0) {
        LOG_ERROR("window_create: invalid desc");
        return NULL;
    }
    app_init();

    OWindow *window = calloc(1, sizeof *window);
    if (!window) {
        LOG_ERROR("window_create: out of memory");
        return NULL;
    }

    NSWindowStyleMask style = desc->undecorated
        ? NSWindowStyleMaskBorderless
        : NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

    // spawn window where cursor is
    NSScreen *screen = NSScreen.mainScreen;
    NSPoint curpos = NSEvent.mouseLocation;
    for (uint32_t i = 0; i < NSScreen.screens.count; i++) {
        screen = NSScreen.screens[i];
        if (curpos.x > screen.frame.origin.x &&
            curpos.x < screen.frame.origin.x + screen.frame.size.width &&
            curpos.y > screen.frame.origin.y &&
            curpos.y < screen.frame.origin.y + screen.frame.size.height){
            break;
        }
    }

    NSRect rect = NSMakeRect(desc->x, desc->y, desc->width, desc->height);
    OMacWindow *ns_window = [[OMacWindow alloc] initWithContentRect:rect
                                                          styleMask:style
                                                            backing:NSBackingStoreBuffered
                                                              defer:NO
                                                             screen:screen];
    ns_window.releasedWhenClosed = NO;
    ns_window.title = desc->title ? @(desc->title) : @"";
    ns_window.acceptsMouseMovedEvents = YES;
    if (desc->transparent) {
        ns_window.opaque = NO;
        ns_window.backgroundColor = NSColor.clearColor;
    }

    OMacView *view = [[OMacView alloc] initWithFrame:ns_window.contentView.bounds];
    view.owner = window;
    view.wantsLayer = YES;
    ns_window.contentView = view;
    ns_window.delegate = view;
    [ns_window makeFirstResponder:view];

    if (@available(macOS 14.0, *)) {
        view.displayLink = [view displayLinkWithTarget:view selector:@selector(onDisplayLink:)];
        [view.displayLink addToRunLoop:NSRunLoop.currentRunLoop forMode:NSRunLoopCommonModes];
    } else {
        LOG_WARN("on_refresh needs macOS 14 or newer and will not fire");
    }

    window->ns_window = (void *)CFBridgingRetain(ns_window);
    window->open = true;

    window_show(window);

    g_app.window_count++;
    return window;
}

void window_destroy(OWindow *window) {
    if (!window) return;

    memset(&window->callbacks, 0, sizeof window->callbacks);
    window_close(window);

    NSWindow *ns_window = CFBridgingRelease(window->ns_window);
    OMacView *view = (OMacView *)ns_window.contentView;
    if (@available(macOS 14.0, *)) {
        [view.displayLink invalidate];
        view.displayLink = nil;
    }
    view.owner = NULL;
    ns_window.delegate = nil;

    free(window);
}

void window_show(OWindow *window) {
    if (window && !window->shown) {
        [ns_window_of(window) makeKeyAndOrderFront:nil];
        [NSApp activate];
        window->shown = true;
    }
}

void window_hide(OWindow *window) {
    if (window) {
        [ns_window_of(window) orderOut:nil];
        window->shown = false;
    }
}

void window_close(OWindow *window) {
    if (window && window->open) [ns_window_of(window) close];
}

void window_set_title(OWindow *window, const char *title) {
    if (window && title) ns_window_of(window).title = @(title);
}

void window_set_position(OWindow *window, int32_t x, int32_t y) {
    if (window) [ns_window_of(window) setFrameOrigin:NSMakePoint(x, y)];
}

void window_set_size(OWindow *window, int32_t width, int32_t height) {
    if (!window || width <= 0 || height <= 0) return;
    NSWindow *ns_window = ns_window_of(window);
    NSRect content = [ns_window contentRectForFrameRect:ns_window.frame];
    content.size = NSMakeSize(width, height);
    [ns_window setFrame:[ns_window frameRectForContentRect:content] display:YES];
}

void window_set_callbacks(OWindow *window, const OWindowCallbacks *callbacks, void *user_data) {
    if (!window) return;
    if (callbacks) {
        window->callbacks = *callbacks;
    } else {
        memset(&window->callbacks, 0, sizeof window->callbacks);
    }
    window->user_data = user_data;
}

static bool write_size(NSSize size, int32_t *out_width, int32_t *out_height) {
    if (out_width) *out_width = (int32_t)size.width;
    if (out_height) *out_height = (int32_t)size.height;
    return true;
}

bool window_get_content_size(const OWindow *window, int32_t *out_width, int32_t *out_height) {
    if (!window || !window->open) return false;
    return write_size(ns_window_of(window).contentView.bounds.size, out_width, out_height);
}

bool window_get_pixel_size(const OWindow *window, int32_t *out_width, int32_t *out_height) {
    if (!window || !window->open) return false;
    NSView *view = ns_window_of(window).contentView;
    return write_size([view convertRectToBacking:view.bounds].size, out_width, out_height);
}

bool window_is_open(const OWindow *window) {
    return window && window->open;
}

void *window_native_handle(const OWindow *window) {
    return window && window->open ? (__bridge void *)ns_window_of(window).contentView : NULL;
}

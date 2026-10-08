#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
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
};

static const OKey k_key_map[128] = {
    [kVK_ANSI_A] = O_KEY_A, [kVK_ANSI_B] = O_KEY_B, [kVK_ANSI_C] = O_KEY_C, [kVK_ANSI_D] = O_KEY_D,
    [kVK_ANSI_E] = O_KEY_E, [kVK_ANSI_F] = O_KEY_F, [kVK_ANSI_G] = O_KEY_G, [kVK_ANSI_H] = O_KEY_H,
    [kVK_ANSI_I] = O_KEY_I, [kVK_ANSI_J] = O_KEY_J, [kVK_ANSI_K] = O_KEY_K, [kVK_ANSI_L] = O_KEY_L,
    [kVK_ANSI_M] = O_KEY_M, [kVK_ANSI_N] = O_KEY_N, [kVK_ANSI_O] = O_KEY_O, [kVK_ANSI_P] = O_KEY_P,
    [kVK_ANSI_Q] = O_KEY_Q, [kVK_ANSI_R] = O_KEY_R, [kVK_ANSI_S] = O_KEY_S, [kVK_ANSI_T] = O_KEY_T,
    [kVK_ANSI_U] = O_KEY_U, [kVK_ANSI_V] = O_KEY_V, [kVK_ANSI_W] = O_KEY_W, [kVK_ANSI_X] = O_KEY_X,
    [kVK_ANSI_Y] = O_KEY_Y, [kVK_ANSI_Z] = O_KEY_Z,

    [kVK_ANSI_0] = O_KEY_0, [kVK_ANSI_1] = O_KEY_1, [kVK_ANSI_2] = O_KEY_2, [kVK_ANSI_3] = O_KEY_3,
    [kVK_ANSI_4] = O_KEY_4, [kVK_ANSI_5] = O_KEY_5, [kVK_ANSI_6] = O_KEY_6, [kVK_ANSI_7] = O_KEY_7,
    [kVK_ANSI_8] = O_KEY_8, [kVK_ANSI_9] = O_KEY_9,

    [kVK_Escape] = O_KEY_ESCAPE, [kVK_Return] = O_KEY_ENTER, [kVK_Tab] = O_KEY_TAB,
    [kVK_Delete] = O_KEY_BACKSPACE, [kVK_Space] = O_KEY_SPACE,

    [kVK_LeftArrow] = O_KEY_LEFT, [kVK_RightArrow] = O_KEY_RIGHT,
    [kVK_UpArrow] = O_KEY_UP, [kVK_DownArrow] = O_KEY_DOWN,
};

static OKey map_key(uint16_t key_code) {
    return key_code < 128 ? k_key_map[key_code] : O_KEY_UNKNOWN;
}

static OModKeys map_modifiers(NSEventModifierFlags flags) {
    OModKeys mods = 0;
    if (flags & NSEventModifierFlagShift) mods |= O_MOD_SHIFT;
    if (flags & NSEventModifierFlagControl) mods |= O_MOD_CONTROL;
    if (flags & NSEventModifierFlagOption) mods |= O_MOD_ALT;
    if (flags & NSEventModifierFlagCommand) mods |= O_MOD_SUPER;
    if (flags & NSEventModifierFlagCapsLock) mods |= O_MOD_CAPS_LOCK;
    return mods;
}

static OMouseButton map_mouse_button(NSInteger button) {
    return button >= 0 && button <= 2 ? (OMouseButton)button : O_MOUSE_BUTTON_OTHER;
}

static OKeyEvent make_key_event(NSEvent *event) {
    const uint16_t key_code = (uint16_t)event.keyCode;
    return (OKeyEvent){
        .key = map_key(key_code),
        .modifiers = map_modifiers(event.modifierFlags),
        .native_key_code = key_code,
        .is_repeat = event.isARepeat,
    };
}

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

- (void)emitTextInput:(NSString *)text {
    OWindow *w = self.owner;
    uint32_t codepoints[32];
    NSUInteger used = 0;
    [text getBytes:codepoints
             maxLength:sizeof codepoints
            usedLength:&used
              encoding:NSUTF32LittleEndianStringEncoding
               options:0
                 range:NSMakeRange(0, text.length)
        remainingRange:NULL];

    for (NSUInteger i = 0; i < used / sizeof codepoints[0]; i++) {
        const uint32_t cp = codepoints[i];
        const bool is_control = cp < 0x20 || cp == 0x7F;
        const bool is_function_key = cp >= 0xF700 && cp <= 0xF8FF;
        if (is_control || is_function_key) continue;
        w->callbacks.on_text_input(w, cp, w->user_data);
    }
}

- (void)keyDown:(NSEvent *)event {
    OWindow *w = self.owner;
    if (!w) return;
    if (w->callbacks.on_key_down) w->callbacks.on_key_down(w, make_key_event(event), w->user_data);

    const bool is_shortcut = (event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
    if (w->callbacks.on_text_input && !is_shortcut) [self emitTextInput:event.characters];
}

- (void)keyUp:(NSEvent *)event {
    OWindow *w = self.owner;
    if (w && w->callbacks.on_key_up) w->callbacks.on_key_up(w, make_key_event(event), w->user_data);
}

- (void)emitMouseMove:(NSEvent *)event {
    OWindow *w = self.owner;
    if (!w || !w->callbacks.on_mouse_move) return;
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    OMouseMoveEvent e = {
        .x = (float)p.x,
        .y = (float)p.y,
        .delta_x = (float)event.deltaX,
        .delta_y = (float)event.deltaY,
    };
    w->callbacks.on_mouse_move(w, e, w->user_data);
}

- (void)emitMouseButton:(NSEvent *)event to:(OnMouseButtonFn)callback {
    OWindow *w = self.owner;
    if (!w || !callback) return;
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    OMouseButtonEvent e = {
        .button = map_mouse_button(event.buttonNumber),
        .modifiers = map_modifiers(event.modifierFlags),
        .x = (float)p.x,
        .y = (float)p.y,
    };
    callback(w, e, w->user_data);
}

- (void)mouseMoved:(NSEvent *)event { [self emitMouseMove:event]; }
- (void)mouseDragged:(NSEvent *)event { [self emitMouseMove:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self emitMouseMove:event]; }
- (void)otherMouseDragged:(NSEvent *)event { [self emitMouseMove:event]; }

- (void)mouseDown:(NSEvent *)event { [self emitMouseButton:event to:self.owner->callbacks.on_mouse_down]; }
- (void)rightMouseDown:(NSEvent *)event { [self emitMouseButton:event to:self.owner->callbacks.on_mouse_down]; }
- (void)otherMouseDown:(NSEvent *)event { [self emitMouseButton:event to:self.owner->callbacks.on_mouse_down]; }
- (void)mouseUp:(NSEvent *)event { [self emitMouseButton:event to:self.owner->callbacks.on_mouse_up]; }
- (void)rightMouseUp:(NSEvent *)event { [self emitMouseButton:event to:self.owner->callbacks.on_mouse_up]; }
- (void)otherMouseUp:(NSEvent *)event { [self emitMouseButton:event to:self.owner->callbacks.on_mouse_up]; }

- (void)scrollWheel:(NSEvent *)event {
    OWindow *w = self.owner;
    if (!w || !w->callbacks.on_mouse_scroll) return;
    OMouseScrollEvent e = {
        .delta_x = (float)event.scrollingDeltaX,
        .delta_y = (float)event.scrollingDeltaY,
    };
    w->callbacks.on_mouse_scroll(w, e, w->user_data);
}

@end

static void app_init(void) {
    if (g_app.initialized) return;

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
    [NSApp activateIgnoringOtherApps:YES];

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
        : NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    if (desc->resizable) style |= NSWindowStyleMaskResizable;

    // spawn window where cursor is
    NSScreen *screen = NSScreen.mainScreen;
    NSPoint curpos = NSEvent.mouseLocation;
    for (uint32_t i = 0; i < NSScreen.screens.count; i++) {
        screen = NSScreen.screens[i];
        if (curpos.x > screen.frame.origin.x &&
            curpos.x < screen.frame.origin.x + screen.frame.origin.x &&
            curpos.y > screen.frame.origin.y &&
            curpos.y < screen.frame.origin.y + screen.frame.origin.y){
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
    if (window) [ns_window_of(window) makeKeyAndOrderFront:nil];
}

void window_hide(OWindow *window) {
    if (window) [ns_window_of(window) orderOut:nil];
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

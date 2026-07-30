#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <math.h>

enum {
    TOKA_GUI_EVENT_NONE = 0,
    TOKA_GUI_EVENT_SHOWN = 1,
    TOKA_GUI_EVENT_POINTER_MOVED = 2,
    TOKA_GUI_EVENT_POINTER_DOWN = 3,
    TOKA_GUI_EVENT_POINTER_UP = 4,
    TOKA_GUI_EVENT_KEY_DOWN = 5,
    TOKA_GUI_EVENT_RESIZED = 6,
    TOKA_GUI_EVENT_CLOSE_REQUESTED = 7,
    TOKA_GUI_EVENT_SCROLLED = 8,
};

enum {
    TOKA_GUI_TEXT_NONE = 0,
    TOKA_GUI_TEXT_COMMITTED = 1,
    TOKA_GUI_TEXT_COMPOSITION = 2,
    TOKA_GUI_TEXT_COMPOSITION_CLEARED = 3,
};

typedef struct {
    int kind;
    double x;
    double y;
    int code;
    int modifiers;
    int width;
    int height;
} TokaGuiEvent;

typedef struct {
    int kind;
    char *text;
} TokaGuiTextEvent;

@class TokaGuiWindowDelegate;
@class TokaGuiView;

typedef struct TokaGuiWindow {
    NSWindow *window;
    id<MTLCommandQueue> command_queue;
    id<MTLRenderPipelineState> rectangle_pipeline;
    id<MTLRenderPipelineState> text_pipeline;
    NSMutableDictionary<NSString *, NSImage *> *image_cache;
    id<CAMetalDrawable> frame_drawable;
    id<MTLCommandBuffer> frame_command_buffer;
    id<MTLRenderCommandEncoder> frame_encoder;
    MTLScissorRect clip_stack[32];
    unsigned int clip_depth;
    TokaGuiWindowDelegate *delegate;
    TokaGuiEvent events[64];
    unsigned int event_head;
    unsigned int event_tail;
    TokaGuiEvent current_event;
    TokaGuiTextEvent text_events[64];
    unsigned int text_event_head;
    unsigned int text_event_tail;
    TokaGuiTextEvent current_text_event;
    char *clipboard_text;
    int redraw_requested;
} TokaGuiWindow;

typedef struct {
    float position[2];
    float color[4];
} TokaGuiVertex;

typedef struct {
    float position[2];
    float uv[2];
} TokaGuiTextVertex;

static NSString *const rectangle_shader_source =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct Vertex { float2 position; float4 color; };\n"
     "struct Raster { float4 position [[position]]; float4 color; };\n"
     "vertex Raster toka_gui_vertex(const device Vertex *vertices [[buffer(0)]], uint index [[vertex_id]]) {\n"
     "  Raster result; result.position = float4(vertices[index].position, 0.0, 1.0); result.color = vertices[index].color; return result;\n"
     "}\n"
     "fragment float4 toka_gui_fragment(Raster input [[stage_in]]) { return input.color; }\n"
     "struct TextVertex { float2 position; float2 uv; };\n"
     "struct TextRaster { float4 position [[position]]; float2 uv; };\n"
     "vertex TextRaster toka_gui_text_vertex(const device TextVertex *vertices [[buffer(0)]], uint index [[vertex_id]]) {\n"
     "  TextRaster result; result.position = float4(vertices[index].position, 0.0, 1.0); result.uv = vertices[index].uv; return result;\n"
     "}\n"
     "fragment float4 toka_gui_text_fragment(TextRaster input [[stage_in]], texture2d<float> atlas [[texture(0)]]) {\n"
     "  constexpr sampler texture_sampler(address::clamp_to_edge, filter::linear); return atlas.sample(texture_sampler, input.uv);\n"
     "}\n";

static TokaGuiWindow *window_from_handle(void *handle) {
    return (TokaGuiWindow *)handle;
}

static void configure_alpha_pipeline(MTLRenderPipelineDescriptor *descriptor) {
    MTLRenderPipelineColorAttachmentDescriptor *attachment = descriptor.colorAttachments[0];
    attachment.pixelFormat = MTLPixelFormatBGRA8Unorm;
    attachment.blendingEnabled = YES;
    attachment.rgbBlendOperation = MTLBlendOperationAdd;
    attachment.alphaBlendOperation = MTLBlendOperationAdd;
    attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
    attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
}

static void push_event(TokaGuiWindow *state, int kind, double x, double y,
                       int code, int modifiers, int width, int height) {
    if (state == NULL) return;
    unsigned int next = (state->event_tail + 1) % 64;
    if (next == state->event_head) {
        state->event_head = (state->event_head + 1) % 64;
    }
    state->events[state->event_tail] = (TokaGuiEvent){kind, x, y, code, modifiers, width, height};
    state->event_tail = next;
}

static void release_text_event(TokaGuiTextEvent *event) {
    if (event->text != NULL) free(event->text);
    event->text = NULL;
    event->kind = TOKA_GUI_TEXT_NONE;
}

static void push_text_event(TokaGuiWindow *state, int kind, NSString *text) {
    if (state == NULL) return;
    const char *utf8 = text == nil ? "" : [text UTF8String];
    char *copy = strdup(utf8 == NULL ? "" : utf8);
    if (copy == NULL) return;
    unsigned int next = (state->text_event_tail + 1) % 64;
    if (next == state->text_event_head) {
        release_text_event(&state->text_events[state->text_event_head]);
        state->text_event_head = (state->text_event_head + 1) % 64;
    }
    state->text_events[state->text_event_tail] = (TokaGuiTextEvent){kind, copy};
    state->text_event_tail = next;
}

@interface TokaGuiView : NSView <NSTextInputClient> {
@public
    TokaGuiWindow *state;
    NSString *marked_text;
}
- (id)initWithFrame:(NSRect)frame state:(TokaGuiWindow *)window_state;
@end

@implementation TokaGuiView
- (id)initWithFrame:(NSRect)frame state:(TokaGuiWindow *)window_state {
    self = [super initWithFrame:frame];
    if (self != nil) state = window_state;
    return self;
}
- (void)dealloc {
    [marked_text release];
    [super dealloc];
}
- (BOOL)acceptsFirstResponder { return YES; }
- (void)keyDown:(NSEvent *)event { [self interpretKeyEvents:@[event]]; }
- (BOOL)hasMarkedText { return marked_text != nil && [marked_text length] != 0; }
- (NSRange)markedRange { return [self hasMarkedText] ? NSMakeRange(0, [marked_text length]) : NSMakeRange(NSNotFound, 0); }
- (NSRange)selectedRange { return NSMakeRange(0, 0); }
- (void)setMarkedText:(id)text selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {
    NSString *value = [text isKindOfClass:[NSAttributedString class]] ? [text string] : text;
    [marked_text release];
    marked_text = [value copy];
    push_text_event(state, TOKA_GUI_TEXT_COMPOSITION, marked_text);
}
- (void)unmarkText {
    [marked_text release];
    marked_text = nil;
    push_text_event(state, TOKA_GUI_TEXT_COMPOSITION_CLEARED, nil);
}
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText { return @[]; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    if (actualRange != NULL) *actualRange = NSMakeRange(NSNotFound, 0);
    return nil;
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point { return 0; }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    if (actualRange != NULL) *actualRange = NSMakeRange(0, 0);
    return [[self window] convertRectToScreen:NSMakeRect(0.0, 0.0, 1.0, 1.0)];
}
- (void)insertText:(id)text replacementRange:(NSRange)replacementRange {
    NSString *value = [text isKindOfClass:[NSAttributedString class]] ? [text string] : text;
    if ([self hasMarkedText]) [self unmarkText];
    push_text_event(state, TOKA_GUI_TEXT_COMMITTED, value);
}
- (void)doCommandBySelector:(SEL)selector {}
@end

@interface TokaGuiWindowDelegate : NSObject <NSWindowDelegate> {
@public
    TokaGuiWindow *state;
}
- (id)initWithState:(TokaGuiWindow *)window_state;
@end

@implementation TokaGuiWindowDelegate
- (id)initWithState:(TokaGuiWindow *)window_state {
    self = [super init];
    if (self != nil) state = window_state;
    return self;
}
- (void)windowDidResize:(NSNotification *)notification {
    NSWindow *window = [notification object];
    NSSize size = [[window contentView] bounds].size;
    push_event(state, TOKA_GUI_EVENT_RESIZED, 0.0, 0.0, 0, 0,
               (int)size.width, (int)size.height);
    state->redraw_requested = 1;
}
- (void)windowWillClose:(NSNotification *)notification {
    push_event(state, TOKA_GUI_EVENT_CLOSE_REQUESTED, 0.0, 0.0, 0, 0, 0, 0);
}
@end

static NSApplication *application(void) {
    NSApplication *app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    return app;
}

void *toka_gui_macos_create_window(const char *title, int width, int height) {
    @autoreleasepool {
        application();
        NSRect frame = NSMakeRect(0.0, 0.0, (CGFloat)width, (CGFloat)height);
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                        styleMask:style
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
        if (window == nil) return NULL;
        [window setReleasedWhenClosed:NO];

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            [window release];
            return NULL;
        }
        id<MTLCommandQueue> command_queue = [device newCommandQueue];
        if (command_queue == nil) {
            [window release];
            return NULL;
        }
        CAMetalLayer *layer = [CAMetalLayer layer];
        [layer setDevice:device];
        [layer setPixelFormat:MTLPixelFormatBGRA8Unorm];
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:rectangle_shader_source options:nil error:&error];
        if (library == nil) {
            [command_queue release];
            [window release];
            return NULL;
        }
        id<MTLFunction> vertex = [library newFunctionWithName:@"toka_gui_vertex"];
        id<MTLFunction> fragment = [library newFunctionWithName:@"toka_gui_fragment"];
        MTLRenderPipelineDescriptor *pipeline_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        [pipeline_descriptor setVertexFunction:vertex];
        [pipeline_descriptor setFragmentFunction:fragment];
        configure_alpha_pipeline(pipeline_descriptor);
        id<MTLRenderPipelineState> rectangle_pipeline = [device newRenderPipelineStateWithDescriptor:pipeline_descriptor error:&error];
        [pipeline_descriptor release];
        [vertex release];
        [fragment release];
        if (rectangle_pipeline == nil) {
            [library release];
            [command_queue release];
            [window release];
            return NULL;
        }
        id<MTLFunction> text_vertex = [library newFunctionWithName:@"toka_gui_text_vertex"];
        id<MTLFunction> text_fragment = [library newFunctionWithName:@"toka_gui_text_fragment"];
        MTLRenderPipelineDescriptor *text_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        [text_descriptor setVertexFunction:text_vertex];
        [text_descriptor setFragmentFunction:text_fragment];
        configure_alpha_pipeline(text_descriptor);
        id<MTLRenderPipelineState> text_pipeline = [device newRenderPipelineStateWithDescriptor:text_descriptor error:&error];
        [text_descriptor release];
        [text_vertex release];
        [text_fragment release];
        [library release];
        if (text_pipeline == nil) {
            [rectangle_pipeline release];
            [command_queue release];
            [window release];
            return NULL;
        }
        TokaGuiView *view = [[TokaGuiView alloc] initWithFrame:frame state:NULL];
        [view setWantsLayer:YES];
        [view setLayer:layer];
        [window setContentView:view];
        [view release];
        [window setTitle:[NSString stringWithUTF8String:title]];
        [window center];
        TokaGuiWindow *result = calloc(1, sizeof(TokaGuiWindow));
        if (result == NULL) {
            [text_pipeline release];
            [rectangle_pipeline release];
            [command_queue release];
            [window release];
            return NULL;
        }
        result->window = window;
        result->command_queue = command_queue;
        result->rectangle_pipeline = rectangle_pipeline;
        result->text_pipeline = text_pipeline;
        ((TokaGuiView *)[window contentView])->state = result;
        result->delegate = [[TokaGuiWindowDelegate alloc] initWithState:result];
        result->image_cache = [[NSMutableDictionary alloc] init];
        if (result->delegate == nil || result->image_cache == nil) {
            [result->image_cache release];
            [result->delegate release];
            [text_pipeline release];
            [rectangle_pipeline release];
            [command_queue release];
            [window release];
            free(result);
            return NULL;
        }
        [window setDelegate:result->delegate];
        return result;
    }
}

int toka_gui_macos_show_window(void *handle) {
    @autoreleasepool {
        TokaGuiWindow *state = window_from_handle(handle);
        if (state == NULL || state->window == nil) return -1;
        NSWindow *window = state->window;
        [window makeKeyAndOrderFront:nil];
        [window makeFirstResponder:[window contentView]];
        [application() activateIgnoringOtherApps:YES];
        push_event(state, TOKA_GUI_EVENT_SHOWN, 0.0, 0.0, 0, 0, 0, 0);
        state->redraw_requested = 1;
        return 0;
    }
}

static int dispatch_event(NSEvent *event) {
    if (event == nil) return 0;
    NSWindow *window = [event window];
    id delegate = [window delegate];
    TokaGuiWindow *state = [delegate isKindOfClass:[TokaGuiWindowDelegate class]]
        ? ((TokaGuiWindowDelegate *)delegate)->state : NULL;
    NSPoint point = [event locationInWindow];
    NSSize content_size = window == nil ? NSMakeSize(0.0, 0.0) : [[window contentView] bounds].size;
    double pointer_x = content_size.width <= 0.0 ? 0.0 : point.x / content_size.width;
    double pointer_y = content_size.height <= 0.0 ? 0.0 : 1.0 - point.y / content_size.height;
    NSUInteger native_modifiers = [event modifierFlags];
    int modifiers = 0;
    if ((native_modifiers & NSEventModifierFlagShift) != 0) modifiers |= 1;
    if ((native_modifiers & NSEventModifierFlagControl) != 0) modifiers |= 2;
    if ((native_modifiers & NSEventModifierFlagOption) != 0) modifiers |= 4;
    if ((native_modifiers & NSEventModifierFlagCommand) != 0) modifiers |= 8;
    switch ([event type]) {
        case NSEventTypeMouseMoved:
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
            push_event(state, TOKA_GUI_EVENT_POINTER_MOVED, pointer_x, pointer_y, 0, modifiers, 0, 0);
            break;
        case NSEventTypeLeftMouseDown:
        case NSEventTypeRightMouseDown:
        case NSEventTypeOtherMouseDown:
            push_event(state, TOKA_GUI_EVENT_POINTER_DOWN, pointer_x, pointer_y,
                       (int)[event buttonNumber], modifiers, 0, 0);
            break;
        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseUp:
            push_event(state, TOKA_GUI_EVENT_POINTER_UP, pointer_x, pointer_y,
                       (int)[event buttonNumber], modifiers, 0, 0);
            break;
        case NSEventTypeKeyDown:
            push_event(state, TOKA_GUI_EVENT_KEY_DOWN, 0.0, 0.0,
                       (int)[event keyCode], modifiers, 0, 0);
            break;
        case NSEventTypeScrollWheel:
            push_event(state, TOKA_GUI_EVENT_SCROLLED, [event scrollingDeltaX], [event scrollingDeltaY],
                       0, modifiers, 0, 0);
            break;
        default:
            break;
    }
    [NSApp sendEvent:event];
    [NSApp updateWindows];
    return 1;
}

int toka_gui_macos_pump_events(void) {
    @autoreleasepool {
        return dispatch_event([NSApp nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:[NSDate date]
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES]);
    }
}

int toka_gui_macos_wait_events(int timeout_millis) {
    @autoreleasepool {
        if (timeout_millis < 0) return -1;
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:(NSTimeInterval)timeout_millis / 1000.0];
        return dispatch_event([NSApp nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:deadline
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES]);
    }
}

int toka_gui_macos_next_event_kind(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->event_head == state->event_tail) return TOKA_GUI_EVENT_NONE;
    state->current_event = state->events[state->event_head];
    state->event_head = (state->event_head + 1) % 64;
    return state->current_event.kind;
}

double toka_gui_macos_event_x(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    return state == NULL ? 0.0 : state->current_event.x;
}

double toka_gui_macos_event_y(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    return state == NULL ? 0.0 : state->current_event.y;
}

int toka_gui_macos_event_code(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    return state == NULL ? 0 : state->current_event.code;
}

int toka_gui_macos_event_modifiers(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    return state == NULL ? 0 : state->current_event.modifiers;
}

int toka_gui_macos_next_text_kind(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL) return TOKA_GUI_TEXT_NONE;
    release_text_event(&state->current_text_event);
    if (state->text_event_head == state->text_event_tail) return TOKA_GUI_TEXT_NONE;
    state->current_text_event = state->text_events[state->text_event_head];
    state->text_events[state->text_event_head] = (TokaGuiTextEvent){TOKA_GUI_TEXT_NONE, NULL};
    state->text_event_head = (state->text_event_head + 1) % 64;
    return state->current_text_event.kind;
}

void *toka_gui_macos_text_value(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    return state == NULL ? NULL : state->current_text_event.text;
}

int toka_gui_macos_event_width(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    return state == NULL ? 0 : state->current_event.width;
}

int toka_gui_macos_event_height(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    return state == NULL ? 0 : state->current_event.height;
}

int toka_gui_macos_window_has_metal(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    NSWindow *window = state == NULL ? nil : state->window;
    return window != nil && state->command_queue != nil && state->rectangle_pipeline != nil && state->text_pipeline != nil && [[window contentView] layer] != nil &&
           [[[window contentView] layer] isKindOfClass:[CAMetalLayer class]];
}

int toka_gui_macos_copy_text(void *handle, const char *text) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->window == nil || text == NULL) return -1;
    @autoreleasepool {
        NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        return [pasteboard setString:[NSString stringWithUTF8String:text]
                              forType:NSPasteboardTypeString] ? 0 : -1;
    }
}

void *toka_gui_macos_paste_text(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->window == nil) return NULL;
    @autoreleasepool {
        NSString *value = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        if (state->clipboard_text != NULL) {
            free(state->clipboard_text);
            state->clipboard_text = NULL;
        }
        if (value == nil) return NULL;
        const char *utf8 = [value UTF8String];
        if (utf8 == NULL) return NULL;
        state->clipboard_text = strdup(utf8);
        return state->clipboard_text;
    }
}

int toka_gui_macos_window_width(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    return state == NULL || state->window == nil ? -1 : (int)[[state->window contentView] bounds].size.width;
}

int toka_gui_macos_window_height(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    return state == NULL || state->window == nil ? -1 : (int)[[state->window contentView] bounds].size.height;
}

int toka_gui_macos_resize_window(void *handle, int width, int height) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->window == nil || width <= 0 || height <= 0) return -1;
    [state->window setContentSize:NSMakeSize((CGFloat)width, (CGFloat)height)];
    state->redraw_requested = 1;
    return 0;
}

int toka_gui_macos_request_redraw(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->window == nil) return -1;
    state->redraw_requested = 1;
    return 0;
}

int toka_gui_macos_take_redraw_request(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->window == nil) return -1;
    int requested = state->redraw_requested;
    state->redraw_requested = 0;
    return requested;
}

int toka_gui_macos_clear_window(void *handle, double red, double green, double blue, double alpha) {
    @autoreleasepool {
        TokaGuiWindow *state = window_from_handle(handle);
        if (state == NULL || state->window == nil || state->command_queue == nil) return -1;
        CAMetalLayer *layer = (CAMetalLayer *)[[state->window contentView] layer];
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (drawable == nil) return -1;

        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(red, green, blue, alpha);

        id<MTLCommandBuffer> command_buffer = [state->command_queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];
        [encoder endEncoding];
        [command_buffer presentDrawable:drawable];
        [command_buffer commit];
        return 0;
    }
}

int toka_gui_macos_begin_frame(void *handle, double red, double green, double blue, double alpha) {
    @autoreleasepool {
        TokaGuiWindow *state = window_from_handle(handle);
        if (state == NULL || state->window == nil || state->command_queue == nil ||
            state->frame_drawable != nil || state->frame_command_buffer != nil || state->frame_encoder != nil) return -1;
        CAMetalLayer *layer = (CAMetalLayer *)[[state->window contentView] layer];
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (drawable == nil) return -1;
        id<MTLCommandBuffer> command_buffer = [state->command_queue commandBuffer];
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(red, green, blue, alpha);
        id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];
        if (encoder == nil) return -1;
        state->frame_drawable = [drawable retain];
        state->frame_command_buffer = [command_buffer retain];
        state->frame_encoder = [encoder retain];
        state->clip_stack[0] = (MTLScissorRect){0, 0, drawable.texture.width, drawable.texture.height};
        state->clip_depth = 1;
        [encoder setScissorRect:state->clip_stack[0]];
        return 0;
    }
}

int toka_gui_macos_frame_rect(void *handle, double x, double y, double width, double height,
                               double red, double green, double blue, double alpha) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->window == nil || state->frame_encoder == nil || state->rectangle_pipeline == nil) return -1;
    CAMetalLayer *layer = (CAMetalLayer *)[[state->window contentView] layer];
    float left = (float)(x * 2.0 - 1.0);
    float right = (float)((x + width) * 2.0 - 1.0);
    float top = (float)(1.0 - y * 2.0);
    float bottom = (float)(1.0 - (y + height) * 2.0);
    float color[4] = {(float)red, (float)green, (float)blue, (float)alpha};
    TokaGuiVertex vertices[6] = {
        {{left, top}, {color[0], color[1], color[2], color[3]}},
        {{right, top}, {color[0], color[1], color[2], color[3]}},
        {{left, bottom}, {color[0], color[1], color[2], color[3]}},
        {{right, top}, {color[0], color[1], color[2], color[3]}},
        {{right, bottom}, {color[0], color[1], color[2], color[3]}},
        {{left, bottom}, {color[0], color[1], color[2], color[3]}},
    };
    id<MTLBuffer> buffer = [layer.device newBufferWithBytes:vertices
                                                      length:sizeof(vertices)
                                                     options:MTLResourceStorageModeShared];
    if (buffer == nil) return -1;
    [state->frame_encoder setRenderPipelineState:state->rectangle_pipeline];
    [state->frame_encoder setVertexBuffer:buffer offset:0 atIndex:0];
    [state->frame_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [buffer release];
    return 0;
}

static int encode_texture(TokaGuiWindow *state, id<MTLTexture> texture,
                          double x, double y, double width, double height) {
    CAMetalLayer *layer = (CAMetalLayer *)[[state->window contentView] layer];
    float left = (float)(x * 2.0 - 1.0);
    float right = (float)((x + width) * 2.0 - 1.0);
    float top = (float)(1.0 - y * 2.0);
    float bottom = (float)(1.0 - (y + height) * 2.0);
    TokaGuiTextVertex vertices[6] = {
        {{left, top}, {0.0f, 0.0f}}, {{right, top}, {1.0f, 0.0f}}, {{left, bottom}, {0.0f, 1.0f}},
        {{right, top}, {1.0f, 0.0f}}, {{right, bottom}, {1.0f, 1.0f}}, {{left, bottom}, {0.0f, 1.0f}},
    };
    id<MTLBuffer> buffer = [[layer device] newBufferWithBytes:vertices length:sizeof(vertices)
                                                           options:MTLResourceStorageModeShared];
    if (buffer == nil) return -1;
    [state->frame_encoder setRenderPipelineState:state->text_pipeline];
    [state->frame_encoder setVertexBuffer:buffer offset:0 atIndex:0];
    [state->frame_encoder setFragmentTexture:texture atIndex:0];
    [state->frame_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [buffer release];
    return 0;
}

int toka_gui_macos_frame_text(void *handle, const char *text, double x, double y, double size,
                              double red, double green, double blue, double alpha) {
    @autoreleasepool {
        TokaGuiWindow *state = window_from_handle(handle);
        if (state == NULL || state->window == nil || state->frame_encoder == nil ||
            state->text_pipeline == nil || text == NULL || size <= 0.0) return -1;
        NSString *value = [NSString stringWithUTF8String:text];
        if (value == nil || [value length] == 0) return value == nil ? -1 : 0;

        NSView *view = [state->window contentView];
        NSSize view_size = [view bounds].size;
        if (view_size.width <= 0.0 || view_size.height <= 0.0) return -1;
        CGFloat point_size = (CGFloat)(size * view_size.height);
        if (point_size < 1.0) point_size = 1.0;
        NSDictionary *attributes = @{
            NSFontAttributeName: [NSFont systemFontOfSize:point_size],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:red green:green blue:blue alpha:alpha],
        };
        NSSize text_size = [value sizeWithAttributes:attributes];
        NSUInteger pixel_width = (NSUInteger)ceil(text_size.width);
        NSUInteger pixel_height = (NSUInteger)ceil(text_size.height);
        if (pixel_width == 0 || pixel_height == 0) return 0;

        NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)pixel_width pixelsHigh:(NSInteger)pixel_height
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
            colorSpaceName:NSCalibratedRGBColorSpace
            bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
            bytesPerRow:0 bitsPerPixel:0];
        if (bitmap == nil) return -1;
        NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:context];
        [[NSColor clearColor] set];
        NSRectFill(NSMakeRect(0.0, 0.0, (CGFloat)pixel_width, (CGFloat)pixel_height));
        NSAffineTransform *transform = [NSAffineTransform transform];
        [transform translateXBy:0.0 yBy:(CGFloat)pixel_height];
        [transform scaleXBy:1.0 yBy:-1.0];
        [transform concat];
        [value drawInRect:NSMakeRect(0.0, 0.0, (CGFloat)pixel_width, (CGFloat)pixel_height)
            withAttributes:attributes];
        [NSGraphicsContext restoreGraphicsState];

        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:pixel_width height:pixel_height mipmapped:NO];
        CAMetalLayer *layer = (CAMetalLayer *)[view layer];
        id<MTLTexture> texture = [[layer device] newTextureWithDescriptor:descriptor];
        if (texture == nil) {
            [bitmap release];
            return -1;
        }
        [texture replaceRegion:MTLRegionMake2D(0, 0, pixel_width, pixel_height)
                    mipmapLevel:0 withBytes:[bitmap bitmapData] bytesPerRow:[bitmap bytesPerRow]];

        int result = encode_texture(state, texture, x, y,
                                    text_size.width / view_size.width,
                                    text_size.height / view_size.height);
        [texture release];
        [bitmap release];
        return result;
    }
}

int toka_gui_macos_frame_image(void *handle, const char *path, double x, double y,
                               double width, double height, double alpha) {
    @autoreleasepool {
        TokaGuiWindow *state = window_from_handle(handle);
        if (state == NULL || state->window == nil || state->frame_encoder == nil ||
            state->frame_drawable == nil || path == NULL || width <= 0.0 || height <= 0.0) return -1;
        NSString *source_path = [NSString stringWithUTF8String:path];
        if (source_path == nil || state->image_cache == nil) return -1;
        NSImage *image = [state->image_cache objectForKey:source_path];
        if (image == nil) {
            image = [[NSImage alloc] initWithContentsOfFile:source_path];
            if (image == nil) return -1;
            [state->image_cache setObject:image forKey:source_path];
            [image release];
            image = [state->image_cache objectForKey:source_path];
        }
        NSUInteger pixel_width = (NSUInteger)ceil(width * state->frame_drawable.texture.width);
        NSUInteger pixel_height = (NSUInteger)ceil(height * state->frame_drawable.texture.height);
        if (pixel_width == 0 || pixel_height == 0) {
            return 0;
        }
        NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)pixel_width pixelsHigh:(NSInteger)pixel_height
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
            colorSpaceName:NSCalibratedRGBColorSpace
            bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
            bytesPerRow:0 bitsPerPixel:0];
        if (bitmap == nil) {
            return -1;
        }
        NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:context];
        [[NSColor clearColor] set];
        NSRectFill(NSMakeRect(0.0, 0.0, (CGFloat)pixel_width, (CGFloat)pixel_height));
        NSAffineTransform *transform = [NSAffineTransform transform];
        [transform translateXBy:0.0 yBy:(CGFloat)pixel_height];
        [transform scaleXBy:1.0 yBy:-1.0];
        [transform concat];
        CGContextSetAlpha([context CGContext], (CGFloat)alpha);
        [image drawInRect:NSMakeRect(0.0, 0.0, (CGFloat)pixel_width, (CGFloat)pixel_height)
                 fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0
           respectFlipped:YES hints:nil];
        [NSGraphicsContext restoreGraphicsState];

        CAMetalLayer *layer = (CAMetalLayer *)[[state->window contentView] layer];
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:pixel_width height:pixel_height mipmapped:NO];
        id<MTLTexture> texture = [[layer device] newTextureWithDescriptor:descriptor];
        if (texture == nil) {
            [bitmap release];
            return -1;
        }
        [texture replaceRegion:MTLRegionMake2D(0, 0, pixel_width, pixel_height)
                    mipmapLevel:0 withBytes:[bitmap bitmapData] bytesPerRow:[bitmap bytesPerRow]];
        int result = encode_texture(state, texture, x, y, width, height);
        [texture release];
        [bitmap release];
        return result;
    }
}

int toka_gui_macos_clear_image_cache(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->window == nil || state->image_cache == nil) return -1;
    [state->image_cache removeAllObjects];
    return 0;
}

int toka_gui_macos_push_clip(void *handle, double x, double y, double width, double height) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->frame_encoder == nil || state->frame_drawable == nil ||
        state->clip_depth == 0 || state->clip_depth >= 32) return -1;
    NSUInteger drawable_width = state->frame_drawable.texture.width;
    NSUInteger drawable_height = state->frame_drawable.texture.height;
    NSUInteger left = (NSUInteger)floor(x * drawable_width);
    NSUInteger top = (NSUInteger)floor(y * drawable_height);
    NSUInteger right = (NSUInteger)ceil((x + width) * drawable_width);
    NSUInteger bottom = (NSUInteger)ceil((y + height) * drawable_height);
    MTLScissorRect parent = state->clip_stack[state->clip_depth - 1];
    NSUInteger parent_right = parent.x + parent.width;
    NSUInteger parent_bottom = parent.y + parent.height;
    if (left < parent.x) left = parent.x;
    if (top < parent.y) top = parent.y;
    if (right > parent_right) right = parent_right;
    if (bottom > parent_bottom) bottom = parent_bottom;
    if (right <= left || bottom <= top) return -1;
    MTLScissorRect clip = (MTLScissorRect){left, top, right - left, bottom - top};
    state->clip_stack[state->clip_depth] = clip;
    state->clip_depth += 1;
    [state->frame_encoder setScissorRect:clip];
    return 0;
}

int toka_gui_macos_pop_clip(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->frame_encoder == nil || state->clip_depth <= 1) return -1;
    state->clip_depth -= 1;
    [state->frame_encoder setScissorRect:state->clip_stack[state->clip_depth - 1]];
    return 0;
}

int toka_gui_macos_end_frame(void *handle) {
    TokaGuiWindow *state = window_from_handle(handle);
    if (state == NULL || state->frame_drawable == nil || state->frame_command_buffer == nil || state->frame_encoder == nil) return -1;
    [state->frame_encoder endEncoding];
    [state->frame_command_buffer presentDrawable:state->frame_drawable];
    [state->frame_command_buffer commit];
    [state->frame_encoder release];
    [state->frame_command_buffer release];
    [state->frame_drawable release];
    state->frame_encoder = nil;
    state->frame_command_buffer = nil;
    state->frame_drawable = nil;
    state->clip_depth = 0;
    return 0;
}

int toka_gui_macos_fill_rect(void *handle, double x, double y, double width, double height,
                              double red, double green, double blue, double alpha) {
    @autoreleasepool {
        TokaGuiWindow *state = window_from_handle(handle);
        if (state == NULL || state->window == nil || state->command_queue == nil || state->rectangle_pipeline == nil) return -1;
        CAMetalLayer *layer = (CAMetalLayer *)[[state->window contentView] layer];
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (drawable == nil) return -1;

        float left = (float)(x * 2.0 - 1.0);
        float right = (float)((x + width) * 2.0 - 1.0);
        float top = (float)(1.0 - y * 2.0);
        float bottom = (float)(1.0 - (y + height) * 2.0);
        float color[4] = {(float)red, (float)green, (float)blue, (float)alpha};
        TokaGuiVertex vertices[6] = {
            {{left, top}, {color[0], color[1], color[2], color[3]}},
            {{right, top}, {color[0], color[1], color[2], color[3]}},
            {{left, bottom}, {color[0], color[1], color[2], color[3]}},
            {{right, top}, {color[0], color[1], color[2], color[3]}},
            {{right, bottom}, {color[0], color[1], color[2], color[3]}},
            {{left, bottom}, {color[0], color[1], color[2], color[3]}},
        };

        id<MTLBuffer> buffer = [layer.device newBufferWithBytes:vertices
                                                          length:sizeof(vertices)
                                                         options:MTLResourceStorageModeShared];
        if (buffer == nil) return -1;
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.08, 0.10, 0.14, 1.0);

        id<MTLCommandBuffer> command_buffer = [state->command_queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:state->rectangle_pipeline];
        [encoder setVertexBuffer:buffer offset:0 atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
        [encoder endEncoding];
        [command_buffer presentDrawable:drawable];
        [command_buffer commit];
        [buffer release];
        return 0;
    }
}

void toka_gui_macos_destroy_window(void *handle) {
    @autoreleasepool {
        TokaGuiWindow *state = window_from_handle(handle);
        if (state == NULL || state->window == nil) return;
        if (state->frame_encoder != nil) {
            toka_gui_macos_end_frame(state);
        }
        NSWindow *window = state->window;
        [window orderOut:nil];
        [window close];
        [window release];
        [state->command_queue release];
        [state->rectangle_pipeline release];
        [state->text_pipeline release];
        [state->image_cache release];
        [state->delegate release];
        unsigned int text_index = state->text_event_head;
        while (text_index != state->text_event_tail) {
            release_text_event(&state->text_events[text_index]);
            text_index = (text_index + 1) % 64;
        }
        release_text_event(&state->current_text_event);
        free(state->clipboard_text);
        state->window = nil;
        state->command_queue = nil;
        state->rectangle_pipeline = nil;
        state->text_pipeline = nil;
        state->image_cache = nil;
        state->delegate = nil;
        free(state);
    }
}

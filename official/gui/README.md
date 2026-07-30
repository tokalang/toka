# `official/gui` v0.1.0

`official/gui` is Toka's macOS GUI vertical slice. It owns an Objective-C
`NSWindow` behind a Toka `Window`, attaches a `CAMetalLayer`, provides a
single-event non-blocking `App::pump`, coalesces redraw requests, clears the
surface through a Metal command buffer, and draws one normalized-coordinate
rectangle primitive.

This is deliberately not a widget toolkit. Its purpose is to qualify the
package/build/FFI/resource path needed before retained UI state, rendering, and
text input are designed.

## Guarantees

- `Window` never exposes its Objective-C handle through the public Toka API.
- `Window` is `@encap` and declares `clone = delete`; its native handle cannot
  be copied or accessed outside the package. Explicit `close` and deterministic
  `drop` close and release the native window exactly once.
- A successful `Window::open` has a system Metal device and `CAMetalLayer`.
- `Window::redraw` submits a Metal clear pass; it does not execute arbitrary
  shader code or retain a drawing command list.
- `Window::fill_rect` draws one rectangle using a private Metal pipeline. Its
  `x`, `y`, `width`, and `height` use a top-left normalized coordinate space.
- `begin_frame`, `frame_rect`, and `end_frame` let a caller submit multiple
  rectangles in one Metal command buffer and present them together.
- `frame_text` submits one static UTF-8 string as an alpha-blended texture in
  the current frame. Its `size` is normalized to the current window height.
- `push_clip` and `pop_clip` provide nested normalized clip regions for the
  active frame; each nested region intersects its parent and is enforced by
  Metal's scissor state.
- `frame_image` loads a local raster image through macOS's image decoder and
  submits it as an alpha-blended texture in the active frame.
- `add_svg_icon` and `frame_svg_icon` make SVG icon intent explicit while using
  the same macOS image decoder. Decoded images are cached per window and path;
  `clear_image_cache` invalidates that cache after a file update.
- `Scene` is a Toka-owned retained rectangle, image, and text node list. It
  renders strictly in `add_*` call order, preserving painter's order across
  primitive kinds. Text and image paths are copied into the scene.
- `Layout::row_weighted` and `column_weighted` divide a `Bounds` by positive
  relative weights without consuming the input weight vector.
- `Window::size` reports the content size in points; `resize` changes that
  size and coalesces one redraw request so callers can recompute layout.
- `Container` owns ordered child nodes in coordinates local to its parent.
  Containers can nest, and `clip = true` limits descendants to the composed
  container bounds.
- `VirtualList::visible` calculates an overscanned `[first, last_exclusive)`
  range and `item_bounds` places only those rows for a caller-managed scroll
  offset. `Scrolled` events report macOS wheel deltas in `Event.x` and
  `Event.y`.
- Pointer events use normalized coordinates with a top-left origin, matching
  `Bounds`. `ScrollState` clamps a caller-owned offset, `FocusState` owns a
  stable integer focus id, and `ButtonState` implements hover, press, and
  click transitions from typed pointer events.
- `ToggleState` flips only after a press/release inside its bounds.
  `ShortcutMap` dispatches a `KeyDown` to a caller-owned action id by physical
  key code and modifier bitmask; use `Modifiers::shift/control/option/command`
  to construct those masks.
- `Window::poll_text` is separate from physical `KeyDown`: it yields committed
  UTF-8 text, IME composition updates, and composition-clear events from a
  macOS `NSTextInputClient` first responder. The caller owns cursor, selection,
  and document mutation state.
- `Scrollbar::thumb` derives a thumb bounds from `ScrollState`; `drag_to`
  maps a pointer position back to a clamped offset. `ShortcutMap::bind_in_scope`
  lets a focus scope override a global chord, with global bindings as fallback.
- `TextSelection` tracks logical anchor/active positions supplied by the
  caller. `copy_text` and `paste_text` bridge UTF-8 strings to the macOS
  clipboard; neither API treats byte offsets as Unicode cursor positions.
- `Layout::inset`, `row`, and `column` return composable `Bounds` values, so
  callers can build nested layouts without native callbacks or DOM state.
- `Window::poll_event` returns typed `Shown`, pointer, keyboard, resize, and
  close-request events from a bounded per-window queue.
- `Window::request_redraw` and `take_redraw_request` coalesce repeated state
  updates into one application-loop render opportunity; showing or resizing a
  window also requests one redraw.

## Non-goals

- Linux and Windows backends.
- Texture atlases, hot-reload file watching, or cross-window image caches.
- Scrollbar visuals, inertial scrolling, text selection/cursor editing, and
  nested shortcut-scope propagation.
- Text shaping/line breaking, Unicode text editing, accessibility, or a framework-owned
  unbounded application loop.

## Qualification

After building Toka, run `python3 official/gui/tests/qualify_macos_spike.py`.
The test requires a logged-in macOS desktop session because it creates and
closes an AppKit window. Set `TOKA_GUI_BUILD_BIN` when the tool binaries are
not in `build/bin`.

`examples/settings.tk` is the reference application. It uses only public GUI
APIs to compose bounded event waiting, redraw scheduling, toggle input,
shortcuts, and a clipped virtual list. The qualification suite type-checks it
through a copied consumer package; run it manually from a desktop session to
exercise its interactive loop.

# `official/gui` Completion Scope

Status: proposed acceptance contract for Toka 1.0.

## Product boundary

`official/gui` is complete when Toka can ship and maintain a native desktop
tool such as a project inspector, settings application, or package browser.
It is not initially an editor engine and does not promise to reproduce the
whole Zed/GPUI feature set.

The package remains a GPU-accelerated retained-mode toolkit. Its public API
must be Toka-native; GPUI is architectural inspiration, not an API
compatibility target.

## Release gates

### macOS reference release

The first supported release targets macOS and must provide all of the
following:

1. App and window lifecycle: multiple windows, blocking run loop, redraw
   coalescing, resize, and deterministic close/drop behavior.
2. Rendering: batched frames; opaque and alpha rectangles; clipping; static
   UTF-8 text; raster images and SVG icons.
3. Retained scene: nested containers, padding, gap, alignment, flexible row
   and column layout, and scrollable virtual lists.
4. Interaction: pointer, keyboard, focus, shortcuts, buttons, toggles, and
   scrolling.
5. Text entry: selection, cursor movement, clipboard, Unicode input, and the
   macOS IME path needed for CJK input.
6. Production evidence: an in-repository Toka settings/inspector reference
   application, package/offline-build replay, lifecycle redlines, and rendered
   smoke coverage.

### Toka 1.0 portability gate

Before `official/gui` itself can be called 1.0, macOS's public API contract
must also be implemented and qualified on Linux. Platform-native details may
differ, but a Toka application must not need conditional rendering or input
code for the supported feature set.

## Explicit non-goals for 1.0

- Code-editor text layout, syntax highlighting, minimaps, or terminal
  emulation.
- Web, mobile, or Windows backends.
- A CSS/HTML language, visual designer, or large themed component catalogue.
- Multiplayer UI synchronization, accessibility automation, complex gesture
  recognition, docking, or animation timelines.

## Completion evidence

The work is done only when these checks are green on every supported platform:

1. `Window` cannot be cloned or expose its native handle; move/drop/early
   return paths release resources exactly once.
2. Native-package source, platform linking, locked dependency replay, and a
   release-package consumer build pass without undeclared toolchain flags.
3. Input, resize, focus, scroll, frame ordering, clipping, text, and text
   entry each have focused integration tests.
4. The reference application is built only with public `official/gui` APIs and
   runs in the same qualification suite.

## Current progress

The macOS spike has completed the first part of item 1 and the first renderer
slice: a non-cloneable window resource, AppKit event delivery, Metal frames,
rectangles, composable nested bounds layout, redraw coalescing, and a small
retained scene with static UTF-8 text, nested frame clipping, and local raster
images plus SVG icons. Ordered retained scene nodes and flexible container
layout are complete. Nested containers, wheel events, and virtual-list range
calculation are complete. Scroll offsets, focus ids, and button interaction
state are complete. Toggles, modifier-aware key events, and shortcut dispatch
are complete. The macOS committed-text and IME-composition event pipeline is
complete, and logical selection ranges plus clipboard access are available.
Unicode cursor editing and CJK end-to-end acceptance remain. Scrollbar geometry/drag mapping and global-or-local shortcut dispatch
are complete. Scrollbar visuals, inertia, and nested shortcut-scope propagation
are the next slices, before any widget design.

An initial `examples/settings.tk` reference application now type-checks through
the package-consumer qualification path using only public APIs. Its interactive
manual-run acceptance, alongside CJK text-entry acceptance, remains required
before this counts as production evidence.

The application event loop may now participate in `std/task` through the
bounded `@HostEventSource` adapter implemented by `App`.  This is a
thread-local, opt-in executor turn: it lets ready tasks, timers, non-blocking
socket readiness, and one AppKit wait cooperate without making `std` depend on
GUI or moving AppKit objects across threads.  It is not yet a cross-platform
co-wait backend or a complete worker-to-UI dispatch model.

# `official/gui` Completion Scope

Status: retained compiler-facing acceptance contract; standalone macOS
`v0.1.0` released, GUI 1.0 incomplete.

The canonical package source, qualification, release, and imported demos now
live in [`tokalang/gui`](https://github.com/tokalang/gui). This document keeps
the package boundary that Toka's compiler and standard library must support; it
is not a second package roadmap or implementation source.

## Product boundary

`official/gui` is complete when Toka can ship and maintain a native desktop
tool such as a project inspector, settings application, or package browser.
It is not initially an editor engine and does not promise to reproduce the
whole Zed/GPUI feature set.

The package remains a GPU-accelerated retained-mode toolkit. Its public API
must be Toka-native; GPUI is architectural inspiration, not an API
compatibility target.

## Release gates

### macOS completion release

A release satisfying this completion contract must provide all of the
following on macOS:

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
6. Production evidence: canonical-repository settings/editor references,
   package/offline-build replay, lifecycle redlines, and rendered smoke
   coverage.

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

## Released 0.1 evidence

The standalone `v0.1.0` package qualification compiles its Toka fixtures,
Objective-C bridge, and AppKit framework smoke, checks AppKit, Metal, and
QuartzCore linkage, and replays the exact `unicode@0.1.1` dependency from only
its locked archive. The canonical repository's editor and settings demos and
the independent
[`toka-examples/registry_gui_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_gui_consumer)
resolve exact GUI and Unicode locks from an empty registry cache and rebuild
offline from only those two archives.

Those hosted gates do not run the applications, open a real window, establish a
usable Metal device or drawable, or qualify interactive input, IME, and CJK
behavior. Linux parity is also absent. The released slice therefore supplies
package/build/FFI evidence, not completion of the macOS completion-release list
or the GUI 1.0 portability gate above.

The compiler-facing integration is the bounded `@HostEventSource` adapter
implemented by `App`. It lets ready tasks, timers, non-blocking socket
readiness, and one AppKit wait cooperate without making `std` depend on GUI or
moving AppKit objects across threads. `std/task::host_mailbox` supplies the
companion data-only worker-to-UI path: `@Send` updates can cross threads while
the inbox remains on the App thread. This is not a cross-platform co-wait
backend, a wakeable native dispatcher, or a closure/callback path into AppKit.

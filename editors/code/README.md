# Toka Extension for Visual Studio Code

This extension provides official language support, syntax highlighting, and LSP integration for the **Toka Programming Language** in Visual Studio Code.

---

## Features

* **Rich Syntax Highlighting**: Fully supports Toka's unique suffix token attributes (`#`, `?`, `!`), ownership sigils (`^`, `~`, `&`, `*`), and Lisp-style kebab-case identifiers.
* **Document Symbols (Outline)**: Instantly index and navigate your Toka `fn`, `shape`, `impl`, and `alias` definitions through the Outline view.
* **Code Formatting**: Native integration with the Toka Formatter (`tokafmt`) for clean, semicolon-free source styling.
* **Language Server (LSP) Support**: Real-time syntax validation, type checking, and compile-time diagnostics powered by `tokalsp`.

---

## Requirements & Setup

To enable formatting and LSP diagnostics, you must have the Toka toolchain compiled on your machine.

### 1. Compile the Toolchain
Run the rebuild script from the Toka repository root to build `tokafmt` and `tokalsp`:
```bash
./rebuild.sh
```

### 2. Environment Configuration (Choose One)

#### Option A: Workspace Mode (Recommended)
If you open the Toka root repository folder directly in VSCode, the extension will automatically look inside the `build/bin/` folder to locate `tokafmt` and `tokalsp`.

#### Option B: Standalone/Global Mode
If you open individual `.tk` files or subfolders, the extension will attempt to run `tokafmt` and `tokalsp` from your system's `PATH`. Add the build bin directory to your shell's profile (e.g., `.bashrc`, `.zshrc`):
```bash
export PATH="/absolute/path/to/toka/build/bin:$PATH"
```

If the binaries cannot be found, the extension will gracefully degrade to syntax highlighting and outline navigation, avoiding diagnostic warning popups.

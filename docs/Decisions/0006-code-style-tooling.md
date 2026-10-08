# 0006 - Code Style Tooling: Pinned clang-format and clang-tidy

## Status
Accepted

## Context
[Code Style](../CodeStyle.md#enforcement) asks for clang-format and clang-tidy pinned to a single LLVM version, so formatting doesn't depend on whose machine runs it. The tools have to run the same way locally (on every platform) and on CI. Encoding the style also exposed a few rules the tools can't express exactly, and a few the doc didn't cover.

## Decision
- **Pinned through PyPI** - clang-format and clang-tidy come from the `clang-format` and `clang-tidy` Python wheels, pinned to LLVM 22.1.8 in `tools/requirements.txt`. The same versions install with one command on Windows, macOS and Linux, unlike distribution packages or the LLVM installers
- **Scripts** - `tools/format.py` checks or fixes formatting, and `tools/tidy.py` runs clang-tidy in parallel from a configured build tree's compilation database. Both refuse to run with any other tool version. CI runs the same scripts
- **clang-tidy runs from the compilation database, not during the build** - CMake's `CXX_CLANG_TIDY` hook passes MSVC command lines that clang-tidy misreads (exceptions disabled), and it slows every compile. The script only needs a configured tree, so CI doesn't build anything for it
- **Rules the tools decide**:
  - Lines are at most 120 columns, with tabs counting as 4
  - Enums always put their values on separate lines: clang-format can only keep a short enum on one line by also putting the opening brace of every enum on the enum's line, which breaks the braces-on-their-own-line rule
  - Constructor initializer lists go on the line after the signature
- **Rules the doc didn't cover**:
  - Public data members of plain structs (such as components) are PascalCase without the `m_` prefix; private and protected members keep `m_`
  - Constants at namespace and class scope are PascalCase; local constants are camelCase
  - clang-tidy can't tell file-static variables from globals, so namespace-scope variables with internal linkage may use either `s_` or `g_`
  - Besides `-Wall -Wextra -Wpedantic`, GCC and Clang warn on shadowing (`-Wshadow`), non-virtual destructors in classes with virtual functions (`-Wnon-virtual-dtor`) and hidden overloads (`-Woverloaded-virtual`)
  - Designated initializers may leave out members that have default member initializers - the natural way to fill in an options struct. Clang 19 warns about that under `-Wmissing-designated-field-initializers`, which is turned off; GCC 14 has no separate flag, so it gets `-Wno-missing-field-initializers`

## Alternatives
- **Distribution packages** (apt `clang-format-22`, Homebrew `llvm`) - different versions on different platforms, and they change underneath us
- **CMake's `CXX_CLANG_TIDY`** - runs only on changed files, but misbehaves with MSVC and adds clang-tidy's time to every compile

## Consequences
- Developers install the tools with `pip install -r tools/requirements.txt`, preferably in a virtual environment
- Upgrading LLVM means bumping both pins together, reformatting the code in the same change, and fixing any new clang-tidy findings

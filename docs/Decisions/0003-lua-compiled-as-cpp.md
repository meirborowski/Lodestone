# 0003 - Lua Compiled as C++

## Status
Accepted

## Context
Lua has no CMake build, so Lodestone builds it from source and decides how. Lua can be compiled as C or as C++, and the choice changes how a Lua error unwinds the stack:
- Compiled as C, `lua_error` uses `longjmp`. When an error passes through C++ stack frames - a script calls a C++ binding, which calls back into Lua, which raises an error - the C++ frames are skipped without running their destructors. That leaks resources and breaks invariants, and is undefined behaviour in C++
- Compiled as C++, Lua raises errors as C++ exceptions, so the stack unwinds normally and every destructor runs

Script errors must never crash or corrupt the engine (see [Scripting](../Features/Scripting.md)), and bindings will hold RAII objects.

## Decision
- Lua 5.4 is compiled as C++ (every Lua source file has `LANGUAGE CXX`)
- sol2 is told so with `SOL_USING_CXX_LUA=1`. With that setting, sol2's defaults are the right ones: C++ exceptions thrown by bindings are caught at the binding boundary and turned into Lua errors carrying their message, and Lua's own error exceptions pass through untouched
- `SOL_ALL_SAFETIES_ON=1` in every configuration: scripts may be untrusted, so a wrong argument type must become a script error, not a crash
- Lua is built without `dlopen` support on Linux and macOS (`LUA_USE_POSIX` rather than `LUA_USE_LINUX`/`LUA_USE_MACOSX`), and with API checks in Debug (`LUA_USE_APICHECK`)
- The smoke tests check all of this: a Lua error raised through a C++ binding runs the binding's destructors, and an exception thrown by a binding becomes a Lua error with its message

## Alternatives
- **Lua compiled as C** - the default build, and what most engines do, but it can't unwind C++ frames safely
- **Catching every error at each binding** (`lua_pcall` around every callback into Lua) - workable, but every binding author has to remember it, and one omission is undefined behaviour

## Consequences
- Lua's headers must be included without `extern "C"` - never include `lua.hpp`. Include Lua through sol2, or `lua.h` directly
- Native Lua modules (C libraries loaded at runtime) can't be used - they'd expect a C build of Lua. The [sandbox](../Features/Scripting.md#sandbox) forbids them anyway
- Lua errors cost a C++ exception, which is slower than `longjmp`. Script errors are rare, so this doesn't matter

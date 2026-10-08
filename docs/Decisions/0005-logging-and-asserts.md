# 0005 - Logging, Asserts and Fatal Errors

## Status
Accepted

## Context
[Code Style](../CodeStyle.md#error-handling) sets the error-handling rules: recoverable errors are `std::expected<T, Error>`, programmer errors are asserts that are compiled out of Dist, and Dist has no developer logging. Milestone 1 implements them, and has to decide:
- What "no developer logging" means in Dist
- What a failed assert does, and how tests can check asserts without ending the test process
- How executables report failures that escape everything else, without hanging CI or an AI agent driving the engine

## Decision
- **Two loggers** (spdlog): the engine logger (`LS_CORE_*` macros) and the app logger (`LS_*` macros), sharing one distributing sink. The editor console, MCP log reading and tests add their own sinks to it
- **Logging is always safe** - the loggers are created on first use and never destroyed, so logging works before `Log::Init()`, after `Log::Shutdown()` and during static destruction. Without sinks, messages go nowhere
- **Dist logging** - trace, debug and info messages are compiled out of Dist. Warnings, errors and critical messages stay, so shipped games can still record problems. Messages at warning level and above are flushed immediately, so they survive a crash
- **Compiled-out code still compiles** - in Dist, assert conditions and the arguments of compiled-out log messages sit in unevaluated `sizeof` expressions. They're type-checked but never run, and variables used only by them don't trigger unused-variable warnings
- **Assert handler** - a failed assert calls a replaceable handler. The default one logs the failure (to the standard error if the log isn't initialized), breaks into an attached debugger, and aborts. On Windows, it first turns off the C runtime's abort dialog and Windows Error Reporting prompt, which would block CI and AI agents. Tests install a recording handler to check asserts without ending the process; a separate test executable checks the default handler
- **`RunMain`** - every executable's `main()` goes through `Lodestone::RunMain()`, which initializes and shuts down the log and turns an exception escaping the program into a logged report and a failing exit code
- **`Error::GetMessageText()`** rather than `GetMessage()`, because `<windows.h>` defines `GetMessage` as a macro

## Alternatives
- **Compiling all logging out of Dist** - warnings and errors are what's needed to diagnose problems in shipped games
- **`assert()` from the standard library** - no message formatting, no logging, no test hook, and controlled by `NDEBUG`, which Release defines
- **Leaving the Windows abort dialog in place** - useful when a person runs a Debug build, but a modal dialog hangs any unattended run; the debugger break covers the interactive case

## Consequences
- Assert conditions must not have side effects - they aren't evaluated in Dist
- Code that needs a message in Dist logs at warning level or above
- The default assert handler ends the process. Only tests may install a handler that returns

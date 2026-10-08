# Decisions

Record every significant design decision here - especially ones made without asking - so the reasoning survives after the session that made it.

- One file per decision, numbered in order: `0001-scripting-language.md`
- Each record has these sections:
  - **Status** - Accepted, or Superseded by a later decision (linked)
  - **Context** - the problem and its constraints
  - **Decision** - what was chosen
  - **Alternatives** - what else was considered, and why it wasn't chosen
  - **Consequences** - what the decision commits us to, and what would make us revisit it
- Don't rewrite an accepted decision. To change it, write a new record that supersedes it, and update the old record's status

## Index
- [0001 - Scripting language: Lua 5.4 with sol2](0001-scripting-language.md)
- [0002 - Build system: presets, configurations and dependencies](0002-build-system.md)
- [0003 - Lua compiled as C++](0003-lua-compiled-as-cpp.md)
- [0004 - MSVC: /std:c++latest until Build Tools 14.52 is stable](0004-msvc-cpp23-switch.md)
- [0005 - Logging, asserts and fatal errors](0005-logging-and-asserts.md)
- [0006 - Code style tooling: pinned clang-format and clang-tidy](0006-code-style-tooling.md)

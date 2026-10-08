# Code Style

- Indent with tabs. Lines are at most 120 columns, with tabs counting as 4
- Opening braces go on their own line for types, functions and control statements. Namespace braces stay on the same line (`namespace Lodestone {`), and namespace contents are indented. Short functions defined in a class body (such as accessors) may be written on one line. Enums always put their values on separate lines (see [Decision 0006](Decisions/0006-code-style-tooling.md))
- Braces may be omitted around a single-statement body
- `#pragma once` in every header; file names match the primary type they contain (`SceneRenderer.h` / `SceneRenderer.cpp`)
- All engine code lives in the `Lodestone` namespace
- Naming:
  - Types (classes, structs, enums, type aliases), functions and methods: `PascalCase`
  - Local variables, local constants and function parameters: `camelCase`
  - Private and protected member variables: `m_` prefix + `PascalCase` (`m_Width`). Public data members of plain structs, such as components, are `PascalCase` without a prefix (`Translation`)
  - Static variables: `s_` prefix (`s_Instance`); globals: `g_` prefix
  - Constants at namespace or class scope: `PascalCase` (`MaxEntities`)
  - Macros: `UPPER_SNAKE_CASE` with an `LS_` prefix (`LS_ASSERT`, `LS_CORE_INFO`)
  - Enums are `enum class` with `PascalCase` values
  - Accessors are `GetX()` / `SetX()`; boolean queries are `IsX()` / `HasX()`
  - No `I` prefix on interfaces or abstract classes
- Ownership via smart pointer aliases: `Ref<T>` (shared) and `Scope<T>` (unique), created with `CreateRef<T>()` / `CreateScope<T>()`; raw pointers are non-owning only
- Class layout: public interface first, then protected, then private; member variables grouped at the bottom
- Use `const` wherever possible; pass non-trivial types by const reference

## Enforcement
Tools enforce this style, so it doesn't depend on anyone remembering it:
- `.clang-format` encodes the formatting rules, and CI fails on unformatted code. clang-format and clang-tidy are pinned to a single LLVM version in `tools/requirements.txt` - different versions format differently. Install them with `pip install -r tools/requirements.txt`
- `.clang-tidy` enforces the naming rules (`readability-identifier-naming`) and bug-prone checks, with every warning an error
- Check formatting with `python tools/format.py` (fix it with `--fix`), and run clang-tidy with `python tools/tidy.py` on a configured build tree. CI runs the same scripts
- Engine code builds with warnings as errors (`/W4 /WX` on MSVC; `-Wall -Wextra -Wpedantic -Werror -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual` on GCC and Clang). Third-party code is excluded: its headers are included as system headers, and the flags aren't applied to its targets
- Designated initializers may leave out members that have default member initializers, so options structs are filled in with only the fields that matter: `Log::Init({.Console = LogConsole::None})`. GCC's and Clang's warnings about that are off
- The tool configs and this doc must agree - if they diverge, fix whichever is wrong

## Example

```cpp
#pragma once

namespace Lodestone {

	enum class ProjectionType
	{
		Perspective,
		Orthographic
	};

	class Camera
	{
	public:
		Camera(float fov, float aspectRatio);

		void SetProjectionType(ProjectionType type);
		ProjectionType GetProjectionType() const { return m_ProjectionType; }
		bool IsPerspective() const { return m_ProjectionType == ProjectionType::Perspective; }

	private:
		void RecalculateProjection();

	private:
		ProjectionType m_ProjectionType = ProjectionType::Perspective;
		float m_FOV = 45.0f;
		float m_AspectRatio = 16.0f / 9.0f;
	};

}
```

## Error Handling
- Recoverable errors (missing or corrupt files, failed asset imports, bad network packets, script errors, failed device creation) are returned as `std::expected<T, Error>` and handled by the caller - they must never crash the engine
- Programmer errors (broken invariants, invalid arguments that indicate a bug) use `LS_ASSERT` / `LS_CORE_ASSERT`, which are active in Debug and Release and compiled out of Dist
- Engine code does not throw exceptions. Exceptions from third-party libraries are caught at the boundary where the library is called and converted to `std::expected` errors
- Never silently ignore an error: handle it, return it, or log it with enough context to diagnose it
- Mark functions that return `std::expected` (and other results that must be checked) `[[nodiscard]]`

```cpp
// TextureImporter.h
class TextureImporter
{
public:
	[[nodiscard]] static std::expected<Ref<Texture>, Error> Import(const std::filesystem::path& path);
};

// TextureImporter.cpp
std::expected<Ref<Texture>, Error> TextureImporter::Import(const std::filesystem::path& path)
{
	if (!std::filesystem::exists(path))
		return std::unexpected(Error(ErrorCode::FileNotFound, path.string()));

	// ...
}
```

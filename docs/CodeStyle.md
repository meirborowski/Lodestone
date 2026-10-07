# Code Style

- Indent with tabs; opening braces go on their own line
- `#pragma once` in every header; file names match the primary type they contain (`SceneRenderer.h` / `SceneRenderer.cpp`)
- All engine code lives in the `Lodestone` namespace
- Naming:
  - Types (classes, structs, enums, type aliases), functions and methods: `PascalCase`
  - Local variables and function parameters: `camelCase`
  - Member variables: `m_` prefix + `PascalCase` (`m_Width`)
  - Static variables: `s_` prefix (`s_Instance`); globals: `g_` prefix
  - Macros: `UPPER_SNAKE_CASE` with an `LS_` prefix (`LS_ASSERT`, `LS_CORE_INFO`)
  - Enums are `enum class` with `PascalCase` values
  - Accessors are `GetX()` / `SetX()`; boolean queries are `IsX()` / `HasX()`
  - No `I` prefix on interfaces or abstract classes
- Ownership via smart pointer aliases: `Ref<T>` (shared) and `Scope<T>` (unique), created with `CreateRef<T>()` / `CreateScope<T>()`; raw pointers are non-owning only
- Class layout: public interface first, then protected, then private; member variables grouped at the bottom
- Use `const` wherever possible; pass non-trivial types by const reference

## Example

```cpp
#pragma once

namespace Lodestone {

	enum class ProjectionType { Perspective, Orthographic };

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

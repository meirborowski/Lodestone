#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Lodestone {

	// A 128-bit universally unique identifier. Entities, assets and other things that files and the network refer to
	// are identified by UUIDs, which stay the same across saving, loading and machines - unlike entity handles.
	// Generated UUIDs are random (RFC 9562 version 4); the default UUID is the nil UUID, which identifies nothing
	class UUID
	{
	public:
		constexpr UUID() = default;
		constexpr UUID(uint64_t high, uint64_t low)
			: m_High(high), m_Low(low)
		{
		}

		// A new random UUID. Thread-safe
		[[nodiscard]] static UUID Generate();
		// A version 4 UUID made from 128 random bits, some of which are replaced by the version and variant. For
		// UUIDs from a generator of one's own, such as a scene's, whose sequence can be replayed
		[[nodiscard]] static UUID FromRandomBits(uint64_t high, uint64_t low);
		// Parses the canonical form, "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" in hexadecimal digits of either case
		[[nodiscard]] static std::optional<UUID> Parse(std::string_view text);

		// The canonical form, in lowercase
		std::string ToString() const;

		constexpr bool IsNil() const { return m_High == 0 && m_Low == 0; }
		constexpr uint64_t GetHigh() const { return m_High; }
		constexpr uint64_t GetLow() const { return m_Low; }

		constexpr auto operator<=>(const UUID& other) const = default;

	private:
		uint64_t m_High = 0;
		uint64_t m_Low = 0;
	};

	// Formats UUIDs in their canonical form in log messages and fmt::format
	inline std::string format_as(const UUID& uuid)
	{
		return uuid.ToString();
	}

}

template <>
struct std::hash<Lodestone::UUID>
{
	size_t operator()(const Lodestone::UUID& uuid) const noexcept
	{
		// Generated UUIDs are random, so mixing the halves is enough
		const uint64_t mixed = uuid.GetHigh() ^ (uuid.GetLow() * 0x9E3779B97F4A7C15ull);
		return static_cast<size_t>(mixed ^ (mixed >> 32));
	}
};

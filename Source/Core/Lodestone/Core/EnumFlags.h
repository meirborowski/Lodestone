#pragma once

#include <type_traits>
#include <utility>

namespace Lodestone {

	// Opts an enum into the bitwise operators below, so its values can be combined as flags:
	//   template <> struct EnableFlagOperators<FieldFlags> : std::true_type {};
	template <typename T>
	struct EnableFlagOperators : std::false_type
	{
	};

	template <typename T>
	concept FlagEnum = std::is_enum_v<T> && EnableFlagOperators<T>::value;

	template <FlagEnum T>
	constexpr T operator|(T left, T right)
	{
		return static_cast<T>(std::to_underlying(left) | std::to_underlying(right));
	}

	template <FlagEnum T>
	constexpr T operator&(T left, T right)
	{
		return static_cast<T>(std::to_underlying(left) & std::to_underlying(right));
	}

	template <FlagEnum T>
	constexpr T operator~(T value)
	{
		return static_cast<T>(~std::to_underlying(value));
	}

	template <FlagEnum T>
	constexpr T& operator|=(T& left, T right)
	{
		left = left | right;
		return left;
	}

	template <FlagEnum T>
	constexpr T& operator&=(T& left, T right)
	{
		left = left & right;
		return left;
	}

	// Whether every bit of flag is set in value
	template <FlagEnum T>
	constexpr bool HasFlag(T value, T flag)
	{
		return (std::to_underlying(value) & std::to_underlying(flag)) == std::to_underlying(flag);
	}

}

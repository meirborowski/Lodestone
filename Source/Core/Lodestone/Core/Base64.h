#pragma once

#include "Lodestone/Core/Error.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Lodestone {

	// Standard Base64 (RFC 4648), with padding - for binary data in JSON, such as screenshots sent over MCP
	std::string EncodeBase64(std::span<const uint8_t> data);
	// Decodes standard Base64. Padding is optional; anything outside the alphabet, or a length no encoding produces,
	// is an error
	[[nodiscard]] std::expected<std::vector<uint8_t>, Error> DecodeBase64(std::string_view text);

}

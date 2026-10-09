#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace Lodestone {

	// Standard Base64 (RFC 4648), with padding - for binary data in JSON, such as screenshots sent over MCP
	std::string EncodeBase64(std::span<const uint8_t> data);

}

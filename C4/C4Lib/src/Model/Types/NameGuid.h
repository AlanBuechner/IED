#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace Scripting::NameGuid
{
	/// The 16 bytes, in Win32 _GUID layout, of the RFC 4122 version 5 UUID of `name` in the URL namespace: the
	/// UUID `uuid.uuid5(uuid.NAMESPACE_URL, name)` gives. __uuidof(T) of a type without [uuid] is this for
	/// "c4:" + its qualified name (D-43).
	std::array<uint8_t, 16> GuidBytes(const std::string& name);
}

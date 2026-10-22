#pragma once
#include "ObjectDescription.h"
#include "Core/Core.h"

namespace Core
{
	ObjectDescription LoadObjectDescriptionFromJsonString(const std::string& json);
	// Input that may be malformed (a message from another process): false, with the reason in `error`, instead
	// of asserting. The whole text must be one value.
	bool TryLoadObjectDescriptionFromJsonString(const std::string& json, ObjectDescription& out, std::string* error = nullptr);
	ObjectDescription LoadObjectDescriptionFromJsonFile(const fs::path& file);
	std::string SaveObjectDescriptionToJson(const ObjectDescription& desc);
}


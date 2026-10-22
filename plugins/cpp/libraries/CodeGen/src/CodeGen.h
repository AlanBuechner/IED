#pragma once
#include "StaticReflection/ReflectionData.h"
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

void WriteCode(const fs::path& path, const Core::String& projectName, const Reflection::ReflectionData& data);

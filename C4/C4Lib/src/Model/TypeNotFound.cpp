#include "Model/TypeNotFound.h"

namespace Scripting
{
	namespace
	{
		struct CppType { const char* name; const char* hint; };

		const CppType kCppTypes[] = {
			{ "auto", "C4 has no 'auto': write the type, as in 'int32 count = 0;'" },
			{ "std", "C4 has no 'std': Bedrock has Core::String, Core::Array<T> and Core::HashMap<K, V> (include \"bedrock.ch\")" },
			{ "string", "C4's string is Core::String (include \"bedrock.ch\")" },
			{ "vector", "C4's growable array is Core::Array<T> (include \"bedrock.ch\")" },
			{ "map", "C4's map is Core::HashMap<K, V> (include \"bedrock.ch\")" },
			{ "unordered_map", "C4's map is Core::HashMap<K, V> (include \"bedrock.ch\")" },
		};
	}

	Core::String TypeNotFound(const Core::String& name)
	{
		for (const CppType& cpp : kCppTypes)
			if (name == cpp.name) return "type not found: " + name + ": " + cpp.hint;
		return "type not found: " + name;
	}
} // namespace Scripting

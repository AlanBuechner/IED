// A value of any reflected type to JSON or YAML text and back in one call (D-72), through reflectvalue.ch's rules:
// numbers, bool, Core::String, enums by name, Core::Array<T>, T[N], Core::HashMap<Core::String, V> and objects,
// nested. Reading fills the fields the text has and leaves the rest; arrays and maps it fills must start empty.
//     Core::String text = Serialization::ToJson(settings, 2);
//     Settings loaded;
//     if (!Serialization::FromJson(text.View(), loaded)) printf("not settings\n");

include "reflectvalue.ch"
include "json.ch"
include "jsonreader.ch"
include "yaml.ch"
include "yamlreader.ch"

namespace Serialization
{
	// `value` as JSON (`indent` 0: on one line).
	template<T> Core::String ToJson(const T& value, int32 indent = 0) {
		DomDoc doc;
		DomNode root = WriteValue(doc, DescribeValue(nameof(T)), (const void*)&value);
		Core::StringBuilder text;
		WriteJson(root, &text, indent);
		return text.ToString();
	}

	// JSON read into `value`; false when the text is not JSON or does not fit T.
	template<T> bool FromJson(Core::StringView text, T& value) {
		DomDoc doc;
		ReadResult read = ReadJson(text, doc);
		return read.Ok() && ReadValue(read.root, DescribeValue(nameof(T)), (void*)&value);
	}

	template<T> Core::String ToYaml(const T& value, int32 indent = 2) {
		DomDoc doc;
		DomNode root = WriteValue(doc, DescribeValue(nameof(T)), (const void*)&value);
		Core::StringBuilder text;
		WriteYaml(root, &text, indent);
		return text.ToString();
	}

	template<T> bool FromYaml(Core::StringView text, T& value) {
		DomDoc doc;
		ReadResult read = ReadYaml(text, doc);
		return read.Ok() && ReadValue(read.root, DescribeValue(nameof(T)), (void*)&value);
	}
}

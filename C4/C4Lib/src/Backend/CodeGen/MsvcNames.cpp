#include "Backend/CodeGen/MsvcNames.h"
#include "Model/TypeHandle.h"
#include <vector>

namespace Scripting
{
	namespace MsvcNames
	{
		namespace
		{
			const Type* StripAlias(const Type* t)
			{
				while (t && t->GetKind() == TypeKind::Alias) t = static_cast<const AliasType*>(t)->GetAliasedType();
				return t;
			}

			std::string Identifier(const Core::String& name)
			{
				std::string out;
				for (char c : name)
					out += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') ? c : '_';
				return out.empty() ? std::string("_") : out;
			}

			// MSVC's number encoding inside template arguments: 1..10 as one digit (value - 1), 0 and the rest as
			// hex with the digits A..P terminated by '@', negatives prefixed with '?'.
			std::string Number(int64 value)
			{
				std::string out = value < 0 ? "?" : "";
				uint64 magnitude = value < 0 ? static_cast<uint64>(-(value + 1)) + 1 : static_cast<uint64>(value);
				if (magnitude >= 1 && magnitude <= 10) return out + static_cast<char>('0' + (magnitude - 1));
				std::string hex;
				for (; magnitude; magnitude >>= 4) hex.insert(hex.begin(), static_cast<char>('A' + (magnitude & 0xF)));
				return out + (hex.empty() ? std::string("A") : hex) + "@";
			}

			std::string Primitive(TypeKind kind)
			{
				switch (kind)
				{
				case TypeKind::Void:    return "X";
				case TypeKind::Bool:    return "_N";
				case TypeKind::Char8:    return "D";
				case TypeKind::Int8:    return "C";
				case TypeKind::UInt8:   return "E";
				case TypeKind::Int16:   return "F";
				case TypeKind::UInt16:  return "G";
				case TypeKind::Int32:   return "H";
				case TypeKind::UInt32:  return "I";
				case TypeKind::Int64:   return "_J";
				case TypeKind::UInt64:  return "_K";
				case TypeKind::Float32: return "M";
				case TypeKind::Float64: return "N";
				case TypeKind::Char16:  return "_S";
				case TypeKind::Char32:  return "_U";
				default:                return "";
				}
			}

			/// One decorated name. MSVC writes each identifier the first time as `name@` and records it; a
			/// later occurrence is its index (0-9) in that table. A template-id (`?$Box@H`) is mangled with a
			/// table of its own and then counts as one name of the enclosing table, as MSVC and clang do.
			class Mangler
			{
			public:
				std::string Encode(const Type* t)
				{
					t = StripAlias(t);
					if (!t) return "X";
					std::string primitive = Primitive(t->GetKind());
					if (!primitive.empty()) return primitive;
					switch (t->GetKind())
					{
					case TypeKind::Object:  return Object(static_cast<const ObjectType*>(t));
					case TypeKind::Union:   return "T" + Qualified(SourceName(Identifier(t->GetName())), {});
					case TypeKind::Enum:    return "W4" + Qualified(SourceName(Identifier(t->GetName())), {});
					case TypeKind::Pointer: return Pointer(static_cast<const PointerType*>(t));
					default:                return C4Only(t);
					}
				}

			private:
				std::string SourceName(const std::string& name)
				{
					for (size_t i = 0; i < m_Names.size(); ++i)
						if (m_Names[i] == name) return std::string(1, static_cast<char>('0' + i));
					if (m_Names.size() < 10) m_Names.push_back(name);
					return name + "@";
				}

				// `<unqualified><scope>...@`: the enclosing scopes innermost first, then the terminator.
				std::string Qualified(const std::string& unqualified, const Core::Vector<Core::String>& enclosing)
				{
					std::string out = unqualified;
					for (uint32 i = enclosing.Count(); i-- > 0; ) out += SourceName(Identifier(enclosing[i]));
					return out + "@";
				}

				std::string Object(const ObjectType* obj)
				{
					const char* tag = obj->GetObjectKind() == ObjectType::ObjectKind::Class ? "V" : "U";
					const std::string name = obj->GetTemplateArguments().Empty() ? Identifier(obj->GetName()) : TemplateId(obj);
					return tag + Qualified(SourceName(name), obj->GetEnclosingNames());
				}

				static std::string TemplateId(const ObjectType* obj)
				{
					Mangler own;
					std::string id = "?$" + own.SourceName(Identifier(obj->GetName()));
					for (const auto& arg : obj->GetTemplateArguments())
						id += arg.type ? own.Encode(arg.type) : "$0" + Number(arg.value.AsInt());
					return id;
				}

				std::string Pointer(const PointerType* p)
				{
					return std::string(p->IsConstReferent() ? "PEB" : "PEA") + Encode(p->GetPointeeType());
				}

				std::string C4Only(const Type* t)
				{
					// Types C++ has no spelling for live in a `__c4` namespace of their own.
					return "U" + Qualified(SourceName(Identifier(t->GetName())), { "__c4" });
				}

				std::vector<std::string> m_Names;
			};
		}

		std::string TypeDescriptorName(const Type* t)
		{
			t = StripAlias(t);
			if (t && t->GetKind() == TypeKind::Pointer) return PointerTo(static_cast<const PointerType*>(t)->GetPointeeType());
			std::string encoded = Mangler().Encode(t);
			bool record = !encoded.empty() && (encoded[0] == 'V' || encoded[0] == 'U' || encoded[0] == 'T' || encoded.rfind("W4", 0) == 0);
			return record ? ".?A" + encoded : "." + encoded;
		}

		std::string PointerTo(const Type* pointee)
		{
			return ".PEA" + Mangler().Encode(pointee);
		}
	}
} // namespace Scripting

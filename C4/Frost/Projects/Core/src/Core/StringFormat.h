#pragma once
#include "Types.h"
#include "Vector.h"
#include "String.h"
#include <sstream>
#include <type_traits>

namespace Core
{
	// ⚠️ PLACEHOLDER RULE: a placeholder is {N} where N is all digits, e.g. {0}, {12}.
	// Any other brace sequence (unterminated '{', empty {}, non-numeric content) renders as
	// LITERAL text - it never discards the message, because callers such as ErrorTracker drop
	// empty diagnostics entirely. To print a literal brace that must not be parsed as a
	// placeholder, escape it: "\{". This applies to StringFormat::Format(), FormatString(), and
	// all logging macros. Examples:  FormatString("Hello {0}!", name)  ✅
	//   FormatString("expected ';' got '{'")  -> "expected ';' got '{'" (literal brace kept)
	class StringFormat
	{
		template <class T>
		struct is_char_ptr
			: std::integral_constant<bool,
			std::is_pointer_v<T>
			&& std::is_same_v<char,
			typename std::remove_cv_t<std::remove_pointer_t<T>>>>
		{};

		template <class T>
		struct is_char_array
			: std::integral_constant<bool,
			std::is_array_v<T>
			&& std::is_same_v<char,
			typename std::remove_cv_t<std::remove_extent_t<T>>>>
		{};


		struct MessageToken
		{
			enum TokenType
			{
				String, Param
			};

			MessageToken(std::string_view string) :
				m_Type(String), m_String(string)
			{}
			MessageToken(uint32_t index) :
				m_Type(Param), m_ParamIndex(index)
			{}

			TokenType m_Type;
			std::string_view m_String;
			uint32_t m_ParamIndex = 0;
		};

		struct TemplateElement
		{
			enum Type {
				UINT8, UINT16, UINT32, UINT64,
				INT8, INT16, INT32, INT64,
				FLOAT32, FLOAT64,
				CHAR, CHARPTR, STRING, STRING_VIEW,
				BOOL,
			} type;

			void* data;

			TemplateElement(Type t, void* d) :
				type(t), data(d)
			{}

			template<typename T>
			static TemplateElement GenTemplateElement(const T& data)
			{


#define CHECK_TYPE(t1, t2) if constexpr (std::is_same_v<T, t1>) return TemplateElement(t2, (void*)&data);
				CHECK_TYPE(uint8, UINT8);
				CHECK_TYPE(uint16, UINT16);
				CHECK_TYPE(uint32, UINT32);
				CHECK_TYPE(uint64, UINT64);

				CHECK_TYPE(int8, INT8);
				CHECK_TYPE(int16, INT16);
				CHECK_TYPE(int32, INT32);
				CHECK_TYPE(int64, INT64);

				CHECK_TYPE(float, FLOAT32);
				CHECK_TYPE(double, FLOAT64);

				CHECK_TYPE(char, CHAR);
				CHECK_TYPE(std::string, STRING);
				CHECK_TYPE(Core::String, STRING);
				CHECK_TYPE(std::string_view, STRING_VIEW);

				CHECK_TYPE(bool, BOOL);

				if constexpr (is_char_ptr<T>::value)
					return TemplateElement(CHARPTR, (void*)data);
				if constexpr (is_char_array<T>::value)
					return TemplateElement(CHARPTR, (void*)&data[0]);
#undef CHECK_TYPE

				return TemplateElement(INT32, (void*)&data);
			}

			void PrintValue(std::stringstream& ss)
			{
#define PRINT_NUMBER(t1, t2) case Type::t1: ss << (*(t2*)data);break;
				switch (type)
				{
					PRINT_NUMBER(UINT8, uint8);
					PRINT_NUMBER(UINT16, uint16);
					PRINT_NUMBER(UINT32, uint32);
					PRINT_NUMBER(UINT64, uint64);

					PRINT_NUMBER(INT8, int8);
					PRINT_NUMBER(INT16, int16);
					PRINT_NUMBER(INT32, int32);
					PRINT_NUMBER(INT64, int64);

					PRINT_NUMBER(FLOAT32, float);
					PRINT_NUMBER(FLOAT64, double);

				case Type::CHAR:
					ss << *(char*)data; break;
				case Type::CHARPTR:
					ss << (char*)data; break;
				case Type::STRING:
					ss << *(std::string*)data; break;
				case Type::STRING_VIEW:
					ss << *(std::string_view*)data; break;
				case Type::BOOL:
					ss << (*(bool*)data ? "True" : "False"); break;
				}
#undef PRINT_NUMBER
			}
		};

		static bool ParseMessage(const char* msg, Core::Vector<MessageToken>& tokens);

	public:
		template<typename... Args>
		static void Format(std::stringstream& ss, const char* msg, const Args&... args)
		{
			Core::Vector<TemplateElement> templateElements;
			templateElements.Reserve(sizeof...(args));
			(templateElements.Push(TemplateElement::GenTemplateElement(args)), ...);

			Core::Vector<MessageToken> tokens;
			if (ParseMessage(msg, tokens))
			{
				for (MessageToken token : tokens)
				{
					if (token.m_Type == MessageToken::String)
						ss << token.m_String;
					else if (token.m_Type == MessageToken::Param)
					{
						if (token.m_ParamIndex >= templateElements.Count())
						{
							ss << std::string("{invalid param index \"" + std::to_string(token.m_ParamIndex) + "\"}");
							return;
						}
						templateElements[token.m_ParamIndex].PrintValue(ss);
					}
				}
			}
		}

		template<typename... Args>
		static String Format(const char* msg, const Args&... args)
		{
			std::stringstream ss;
			Format(ss, msg, args...);
			return ss.str();
		}
	};

	template<typename... Args>
	static inline String FormatString(const char* msg, const Args&... args) 
	{
		return StringFormat::Format(msg, args...);
	}

	template<typename... Args>
	static inline String FormatString(const std::string& msg, const Args&... args)
	{
		return StringFormat::Format(msg.c_str(), args...);
	}
	
}

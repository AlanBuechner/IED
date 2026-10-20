#pragma once

#include "Core/Common.h"
#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/StringFormat.h"
#include <iostream>
#include <ctime>
#include <memory>
#include <utility>

#include <sstream>

#ifdef ENABLE_PROFILING
extern "C" {
        void TracyMessageC(const char* txt, size_t size, uint32_t color);
}
#endif

namespace Logging
{
	class Logger
	{
	public:
		enum Level
		{
			Trace, Info, Warn, Error
		};

		struct PatternToken
		{
			enum TokenType
			{
				String, Time, Message
			};

			PatternToken(TokenType type, std::string_view string = "") :
				m_Type(type), m_String(string)
			{}

			TokenType m_Type;
			std::string_view m_String;

		};

		Logger() = default;
		Logger(const std::string& pattern);

		void SetPattern(const std::string& pattern);

		void SetLevel(Level level);

		template<typename... Args>
		void Log(Level level, const char* msg, const Args&... args) const
		{
			if (level >= m_Level)
			{
				std::stringstream ss;
				for (PatternToken token : m_PatternTokens)
				{
					switch (token.m_Type)
					{
					case PatternToken::String:
						ss << token.m_String;
						break;
					case PatternToken::Message:
						Core::StringFormat::Format(ss, msg, args...);
						break;
					case PatternToken::Time:
						time_t now = time(0);
						tm* ltm = localtime(&now);
						ss << ltm->tm_hour << ":" << ltm->tm_min << ":" << ltm->tm_sec;
						break;
					}
				}
				SetConsoleColor(level);
				std::string msg = ss.str();
				std::cout << msg << std::endl;

				uint32 tracyColors[] = { 0xffffff, 0x34eb40, 0xdeeb34, 0xeb3434 };
#if ENABLE_PROFILING
				TracyMessageC(msg.c_str(), msg.size(), tracyColors[(uint32)level]);
#endif
				SetConsoleColor(Level::Trace); // reset console window color

			}
		}

		static void SetConsoleColor(Level level);

	private:
		bool CheckPatternSpecial(uint32 findex);

		void ParsePattern();

	private:
		std::string m_Pattern;
		Core::Vector<PatternToken> m_PatternTokens;
		Level m_Level = Trace;
	};

	class Log
	{
	public:

		static const Logger* GetCoreLogger();
		static const Logger* GetClientLogger();

	};
}

#if defined(IMPLEMENT_LOGGER)


const Logging::Logger* Logging::Log::GetCoreLogger()
{
	static const Logger coreLogger("[%t] ENGINE: %m"); return &coreLogger;
}


const Logging::Logger* Logging::Log::GetClientLogger()
{
	static const Logger clientLogger("[%t] APP: %m"); return &clientLogger;
}

#endif

// All logging macros use StringFormat internally.
// Every placeholder MUST have a numeric index: {0}, {1}, etc. Bare {} is INVALID.
#define CORE_TRACE(...)		::Logging::Log::GetCoreLogger()->Log(::Logging::Logger::Trace, __VA_ARGS__)
#define CORE_INFO(...)		::Logging::Log::GetCoreLogger()->Log(::Logging::Logger::Info, __VA_ARGS__)
#define CORE_WARN(...)		::Logging::Log::GetCoreLogger()->Log(::Logging::Logger::Warn, __VA_ARGS__)
#define CORE_ERROR(...)		::Logging::Log::GetCoreLogger()->Log(::Logging::Logger::Error, __VA_ARGS__)

#define DEBUG_TRACE(...)	::Logging::Log::GetClientLogger()->Log(::Logging::Logger::Trace, __VA_ARGS__)
#define DEBUG_INFO(...)		::Logging::Log::GetClientLogger()->Log(::Logging::Logger::Info, __VA_ARGS__)
#define DEBUG_WARN(...)		::Logging::Log::GetClientLogger()->Log(::Logging::Logger::Warn, __VA_ARGS__)
#define DEBUG_ERROR(...)	::Logging::Log::GetClientLogger()->Log(::Logging::Logger::Error, __VA_ARGS__)

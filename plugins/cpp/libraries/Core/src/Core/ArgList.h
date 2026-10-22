#pragma once
#include "Core/Vector.h"
#include "Core/String.h"

namespace Core
{
	struct Arg
	{
		Core::String name;
		Core::String value;
	};

	class ArgList
	{
	public:
		ArgList() = default;
		ArgList(const Core::Vector<Core::String>& args);

		const Arg* GetArg(const Core::String& str);
		bool HasFlag(const Core::String& str);

	public:
		static Core::ArgList s_GlobalArgs;

	private:
		Core::Vector<Arg> m_Args;
		Core::Vector<Core::String> m_Flags;
	};
}


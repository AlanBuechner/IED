#include "ArgList.h"
#include "Core/Core.h"
#include "Math/Math.h"

Core::ArgList Core::ArgList::s_GlobalArgs;

namespace Core
{
	ArgList::ArgList(const Core::Vector<Core::String>& args)
	{
		auto fixArgFormating = [](Core::String& arg) {
			if (arg[0] == '/' || arg[0] == '\\')
				arg[0] = '-';
			if (arg[1] == '-') arg.erase(1);
		};

		for (uint32 i = 0; i < args.Count(); i++)
		{
			Core::String arg = args[i];
			fixArgFormating(arg);
			
			if (arg[0] == '-') {
				uint64 split = arg.find('=');
				m_Args.Push({ arg.substr(0,split), arg.substr(split+1, arg.size())});
			}
			else {
				m_Flags.Push(arg);
			}
		}
	}

	const Arg* ArgList::GetArg(const Core::String& str)
	{
		if (str[0] == '-'){
			for (Arg& arg : m_Args) {
				if (arg.name == str)
					return &arg;
			}
		}
		return nullptr;
	}

	bool ArgList::HasFlag(const Core::String& str)
	{
		if (str[0] == '-')
		{
			CORE_WARN("Flags dont start with \"-\" try using GetArg()");
			return false;
		}

		return m_Flags.Contains(str);
	}

}
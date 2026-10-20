#include "ArgListTest.h"

namespace Testing
{
	void ArgListTest::TestGetArg()
	{
		Core::Vector<Core::String> rawArgs;
		rawArgs.Push("-mode=debug");

		Core::ArgList args(rawArgs);
		const Core::Arg* arg = args.GetArg("-mode");

		if (arg == nullptr)
			TEST_FAILED("Expected to find -mode argument");
		if (arg->value != "debug")
		{
			Core::String msg = Core::FormatString("Expected mode=debug, got {0}", arg->value);
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	void ArgListTest::TestGetArgValue()
	{
		Core::Vector<Core::String> rawArgs;
		rawArgs.Push("-width=1920");
		rawArgs.Push("-height=1080");
		rawArgs.Push("fullscreen");

		Core::ArgList args(rawArgs);

		const Core::Arg* width = args.GetArg("-width");
		if (width == nullptr || width->value != "1920")
		{
			Core::String msg;
			if (width)
				msg = Core::FormatString("Expected width=1920, got {0}", width->value);
			else
				msg = Core::FormatString("Expected width=1920, got nullptr");
			TEST_FAILED(msg);
		}

		const Core::Arg* height = args.GetArg("-height");
		if (height == nullptr || height->value != "1080")
		{
			Core::String msg;
			if (height)
				msg = Core::FormatString("Expected height=1080, got {0}", height->value);
			else
				msg = Core::FormatString("Expected height=1080, got nullptr");
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	

	void ArgListTest::TestGetArgFlag()
	{
		Core::Vector<Core::String> rawArgs;
		rawArgs.Push("-width=1920");
		rawArgs.Push("-height=1080");
		rawArgs.Push("fullscreen");

		Core::ArgList args(rawArgs);

		if (!args.HasFlag("fullscreen"))
			TEST_FAILED("Expected fullscreen flag to be present");

		TEST_PASSED();
	}


	void ArgListTest::TestGetMissingArg()
	{
		Core::Vector<Core::String> rawArgs;
		rawArgs.Push("-mode=debug");

		Core::ArgList args(rawArgs);
		const Core::Arg* arg = args.GetArg("-missing");

		if (arg != nullptr)
			TEST_FAILED("Expected nullptr for missing argument");

		TEST_PASSED();
	}

}

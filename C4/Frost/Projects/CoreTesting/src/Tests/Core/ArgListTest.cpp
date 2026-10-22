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

	void ArgListTest::TestDoubleDashAndSlash()
	{
		Core::Vector<Core::String> rawArgs;
		rawArgs.Push("--filter=Vector.*");
		rawArgs.Push("/mode=debug");

		Core::ArgList args(rawArgs);
		const Core::Arg* filter = args.GetArg("-filter");
		if (filter == nullptr || filter->value != "Vector.*")
			TEST_FAILED(Core::FormatString("Expected --filter=Vector.* as -filter, got {0}", filter ? filter->value : "nullptr"));
		const Core::Arg* mode = args.GetArg("-mode");
		if (mode == nullptr || mode->value != "debug")
			TEST_FAILED(Core::FormatString("Expected /mode=debug as -mode, got {0}", mode ? mode->value : "nullptr"));

		TEST_PASSED();
	}

	void ArgListTest::TestEmptyArgument()
	{
		Core::Vector<Core::String> rawArgs;
		rawArgs.Push("");
		rawArgs.Push("-");
		rawArgs.Push("-mode=debug");

		Core::ArgList args(rawArgs);
		const Core::Arg* mode = args.GetArg("-mode");
		if (mode == nullptr || mode->value != "debug")
			TEST_FAILED("Expected -mode=debug after an empty argument and a lone dash");

		TEST_PASSED();
	}

}

#include "ForwardDeclarationVisibilityTest.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/Toolchain.h"

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const char* kWidgetHeader = "include \"bedrock.ch\"\nnamespace N\n{\n\tclass Widget\n\t{\n\t\tpublic void Touch();\n\t\tpublic int32 m_Count;\n\t}\n}\n";
		const char* kWidgetSource = "include \"Widget.ch\"\nnamespace N\n{\n\tpublic void Widget::Touch() { m_Count += 1; }\n}\n";
		const char* kHolderHeader = "include \"bedrock.ch\"\nnamespace N\n{\n\tclass Widget;\n\tclass Holder\n\t{\n\t\tpublic void Use(Widget& widget);\n\t}\n}\n";
		const char* kHolderSource = "include \"Holder.ch\"\ninclude \"Widget.ch\"\nnamespace N\n{\n\tpublic void Holder::Use(Widget& widget) { widget.Touch(); }\n}\n";
		const char* kMain = "include \"Holder.ch\"\ninclude \"Widget.ch\"\nint32 main() { N::Widget widget; N::Holder holder; holder.Use(widget); return widget.m_Count - 1; }\n";

		struct Source
		{
			const char* name;
			const char* text;
		};

		// The files registered in `order` (the order the compiler meets them), analyzed: its errors' text.
		Core::String Analyze(const std::vector<Source>& order)
		{
			CompilerInstance compiler;
			for (const Source& source : order) compiler.RegisterInMemory(source.name, source.text);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			Core::String said;
			for (const CompilationError& diagnostic : compiler.GetAllDiagnostics())
				if (diagnostic.severity != ErrorSeverity::Warning) said += diagnostic.message + "\n";
			return said;
		}
	}

	void ForwardDeclarationVisibilityTest::TheDefinitionsFileNamesItWhenTheForwardDeclarationComesFirst()
	{
		Core::String said = Analyze({ { "Holder.ch", kHolderHeader }, { "Holder.cf", kHolderSource }, { "main.cf", kMain },
			{ "Widget.ch", kWidgetHeader }, { "Widget.cf", kWidgetSource } });
		TEST_ASSERT_MSG(said.empty(), "{0}", said.c_str());
		said = Analyze({ { "Widget.ch", kWidgetHeader }, { "Widget.cf", kWidgetSource }, { "Holder.ch", kHolderHeader },
			{ "Holder.cf", kHolderSource }, { "main.cf", kMain } });
		TEST_ASSERT_MSG(said.empty(), "{0}", said.c_str());
	}

	void ForwardDeclarationVisibilityTest::AFileIncludingOnlyTheForwardDeclarationNamesIt()
	{
		const char* user = "include \"Holder.ch\"\nvoid Pass(N::Holder& holder, N::Widget& widget) { holder.Use(widget); }\n";
		for (int definitionFirst = 0; definitionFirst < 2; ++definitionFirst)
		{
			std::vector<Source> order = { { "Holder.ch", kHolderHeader }, { "Holder.cf", kHolderSource }, { "user.cf", user }, { "main.cf", kMain } };
			const std::vector<Source> widget = { { "Widget.ch", kWidgetHeader }, { "Widget.cf", kWidgetSource } };
			order.insert(definitionFirst ? order.begin() : order.end(), widget.begin(), widget.end());
			Core::String said = Analyze(order);
			TEST_ASSERT_MSG(said.empty(), "{0}", said.c_str());
		}
	}

	void ForwardDeclarationVisibilityTest::AFileIncludingNeitherCannotNameIt()
	{
		CompilerInstance compiler;
		const uint32 holder = compiler.RegisterInMemory("Holder.ch", kHolderHeader);
		compiler.RegisterInMemory("Holder.cf", kHolderSource);
		const uint32 widget = compiler.RegisterInMemory("Widget.ch", kWidgetHeader);
		const uint32 widgetSource = compiler.RegisterInMemory("Widget.cf", kWidgetSource);
		const uint32 stranger = compiler.RegisterInMemory("stranger.cf", "include \"bedrock.ch\"\nint32 Seven() { return 7; }\n");
		compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		const ProgramModel& model = compiler.GetProgramModel();
		TEST_ASSERT(model.QueryType("N::Widget", holder).Ok());
		TEST_ASSERT(model.QueryType("N::Widget", widget).Ok());
		TEST_ASSERT(model.QueryType("N::Widget", widgetSource).Ok());
		TEST_ASSERT_MSG(!model.QueryType("N::Widget", stranger).Ok(), "{0}", "a file that includes neither header still cannot name the type");
	}
} // namespace Testing

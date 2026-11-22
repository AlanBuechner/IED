#include "ScriptReachTest.h"
#include "Script/ScriptReach.h"
#include <algorithm>
#include <memory>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		// Projects Core, Engine (on Core), Tools (on Engine) and the root Game (on Engine and Tools); one file each
		// (units 1 to 4), then a library file (unit 5).
		HostInterface Host()
		{
			HostInterface host;
			host.projects = { { "Core", {}, {} }, { "Engine", { 0 }, {} }, { "Tools", { 1 }, {} }, { "Game", { 1, 2 }, {} } };
			host.rootProject = 3;
			for (uint32 p = 0; p < 4; ++p)
			{
				InterfaceFile file;
				file.unitId = p + 1;
				file.project = p;
				file.path = host.projects[p].name + ".ch";
				host.files.push_back(file);
			}
			InterfaceFile library;
			library.unitId = 5;
			library.library = true;
			library.path = "Container/array.ch";
			library.templates = { "Core::Array" };
			host.files.push_back(library);
			return host;
		}

		std::shared_ptr<ScriptChain> Layer(const HostInterface& host, std::string project, std::vector<std::string> depends, uint32 unit,
			std::shared_ptr<ScriptChain> below)
		{
			auto chain = std::make_shared<ScriptChain>(HostIndex(host), HostSymbolTable());
			chain->parent = below;
			chain->layer.project = std::move(project);
			chain->layer.depends = std::move(depends);
			InterfaceFile file;
			file.unitId = unit;
			file.path = chain->layer.project + ".ch";
			chain->layer.files.push_back(file);
			return chain;
		}

		bool Has(const std::vector<uint32>& units, uint32 unit) { return std::find(units.begin(), units.end(), unit) != units.end(); }
	}

	void ScriptReachTest::AnAllowListOfProjectsIsWhatItDependsOn()
	{
		const HostInterface host = Host();
		TEST_ASSERT(ScriptDependencies(host, {}, { "Engine" }, false) == std::vector<std::string>{ "Engine" });
		TEST_ASSERT(ScriptDependencies(host, {}, { "Engine", "Core", "Engine" }, true) == (std::vector<std::string>{ "Engine", "Core" }));
		TEST_PASSED();
	}

	void ScriptReachTest::APathOrNoListDependsOnEverythingBelow()
	{
		const HostInterface host = Host();
		const auto layer = Layer(host, "Kernel", { "Engine" }, 6, nullptr);
		const std::vector<std::string> below = { "Game", "Kernel" };
		TEST_ASSERT(ProjectsBelow(host, { layer.get() }) == below);
		TEST_ASSERT(ScriptDependencies(host, { layer.get() }, { "api/" }, true) == below); // a path names no project
		TEST_ASSERT(ScriptDependencies(host, { layer.get() }, { "Engine", "Elsewhere" }, true) == below);
		TEST_ASSERT(ScriptDependencies(host, { layer.get() }, {}, false) == below); // no list: every host header
		TEST_ASSERT(ScriptDependencies(host, { layer.get() }, { "Kernel" }, true) == std::vector<std::string>{ "Kernel" });
		TEST_PASSED();
	}

	void ScriptReachTest::NothingAllowedDependsOnNothing()
	{
		const HostInterface host = Host();
		TEST_ASSERT(ScriptDependencies(host, {}, {}, true).empty());
		const std::vector<uint32> reached = ReachedUnits(host, {}, {});
		TEST_ASSERT(reached == std::vector<uint32>{ 5 }); // the library alone
		TEST_PASSED();
	}

	void ScriptReachTest::ACompileReadsWhatItsProjectReachesAndTheLibrary()
	{
		const HostInterface host = Host();
		TEST_ASSERT(ReachedUnits(host, {}, { "Engine" }) == (std::vector<uint32>{ 1, 2, 5 }));
		TEST_ASSERT(ReachedUnits(host, {}, { "Tools" }) == (std::vector<uint32>{ 1, 2, 3, 5 }));
		TEST_ASSERT(ReachedUnits(host, {}, ProjectsBelow(host, {})) == (std::vector<uint32>{ 1, 2, 3, 4, 5 }));
		TEST_PASSED();
	}

	// Tools specializes Engine's Box: a script on Engine alone reads Tools too, and what Tools reaches.
	void ScriptReachTest::AFileAddingToATemplateItReadsIsRead()
	{
		HostInterface host = Host();
		host.files[1].templates = { "Engine::Box" };
		host.files[2].extendedTemplates = { "Engine|Box" };
		const std::vector<uint32> reached = ReachedUnits(host, {}, { "Engine" });
		TEST_ASSERT(reached == (std::vector<uint32>{ 1, 2, 3, 5 }));
		host.files[2].extendedTemplates = { "|Engine::Box" }; // written qualified at file scope
		TEST_ASSERT(Has(ReachedUnits(host, {}, { "Engine" }), 3));
		host.files[2].extendedTemplates = { "Tools::Inner|Box" }; // `Box` from Tools::Inner: Tools::Inner::Box, Tools::Box, Box
		TEST_ASSERT(!Has(ReachedUnits(host, {}, { "Engine" }), 3));
		TEST_PASSED();
	}

	// Sol::Stack is not Core::Stack: a specialization of the one leaves the other's readers alone.
	void ScriptReachTest::AnAdditionToAnotherNamespacesTemplateIsNot()
	{
		HostInterface host = Host();
		host.files[0].templates = { "Core::Stack" };
		host.files[2].templates = { "Tools::Stack" };
		host.files[3].extendedTemplates = { "Tools|Stack" };
		const std::vector<uint32> reached = ReachedUnits(host, {}, { "Core" });
		TEST_ASSERT(reached == (std::vector<uint32>{ 1, 5 }));
		TEST_PASSED();
	}

	void ScriptReachTest::EveryUnitOfACollidedSymbolIsRead()
	{
		HostInterface host = Host();
		host.collidedSymbols = { { "Engine_Not_0", { 2, 4 } } };
		TEST_ASSERT(ReachedUnits(host, {}, { "Engine" }) == (std::vector<uint32>{ 1, 2, 3, 4, 5 })); // Game, and what it reaches
		TEST_ASSERT(ReachedUnits(host, {}, { "Core" }) == (std::vector<uint32>{ 1, 5 }));             // none of its units read
		TEST_PASSED();
	}

	// Kernel (on Engine) and Shapes (on Kernel) over the host: a script on Shapes reads them and Engine's reach, not Tools.
	void ScriptReachTest::ALayerReachesWhatItDependsOn()
	{
		const HostInterface host = Host();
		const auto kernel = Layer(host, "Kernel", { "Engine" }, 6, nullptr);
		const auto shapes = Layer(host, "Shapes", { "Kernel" }, 7, kernel);
		const std::vector<const ScriptChain*> layers = { kernel.get(), shapes.get() };
		TEST_ASSERT(ReachedUnits(host, layers, { "Shapes" }) == (std::vector<uint32>{ 1, 2, 5, 6, 7 }));
		TEST_ASSERT(ReachedUnits(host, layers, { "Kernel" }) == (std::vector<uint32>{ 1, 2, 5, 6 }));
		const auto open = Layer(host, "Open", ProjectsBelow(host, {}), 6, nullptr); // a layer that may name everything below it
		TEST_ASSERT(ReachedUnits(host, { open.get() }, { "Open" }) == (std::vector<uint32>{ 1, 2, 3, 4, 5, 6 }));
		TEST_PASSED();
	}
} // namespace Testing

#include "ScriptCompilerTest.h"
#include "TestHosts.h"
#include "Script/ScriptJit.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#include <filesystem>
#include <iostream>
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const char* kGameHeader =
			"namespace Game\n"
			"{\n"
			"\tclass Entity { public int32 hp; public virtual int32 Damage(int32 amount); }\n"
			"\ttemplate<T> class Box { public T v; public T Get() { return v; } }\n"
			"\tthread_local int32 t_frames = 0;\n"
			"\tint32 g_score = 0;\n"
			"\tint32 Add(int32 a, int32 b);\n"
			"\tclass Failure { public int32 code; [operator(\"delete\")] public void Drop() { code = 0; } }\n"
			"\tvoid Fail(int32 code);\n"
			"\tint32 First(int32 v);\n"
			"}\n";

		const char* kGameSource =
			"include \"game.ch\"\n"
			"namespace Game\n"
			"{\n"
			"\tint32 Entity::Damage(int32 amount) { hp -= amount; return hp; }\n"
			"\tint32 Add(int32 a, int32 b) { return a + b; }\n"
			"\tvoid Fail(int32 code) { if (code != 0) throw Failure(); }\n"
			"\tint32 Fourth(int32 v) { return v + 4; }\n"
			"\tint32 Third(int32 v) { return Fourth(v) + 3; }\n"
			"\tint32 Second(int32 v) { return Third(v) + 2; }\n"
			"\tint32 First(int32 v) { return Second(v) + 1; }\n"
			"}\n"
			"int32 main() { Game::Box<int32> b; b.v = Game::Add(1, 2); Game::t_frames = b.Get() + Game::First(1); return 0; }\n";

		const TestHost& Host()
		{
			static const TestHost host = MakeTestHost({ { "game.ch", kGameHeader }, { "game.cf", kGameSource } });
			return host;
		}

		bool Defines(const llvm::Module& module, const char* name)
		{
			const llvm::Function* f = module.getFunction(name);
			return f && !f->isDeclaration() && !f->hasAvailableExternallyLinkage();
		}

		bool Calls(const llvm::Function& function, const char* callee)
		{
			for (const llvm::BasicBlock& block : function)
				for (const llvm::Instruction& instruction : block)
					if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
						if (call->getCalledFunction() && call->getCalledFunction()->getName() == callee) return true;
			return false;
		}

		std::vector<const llvm::CallBase*> CallsOf(const llvm::Function& function, const char* callee)
		{
			std::vector<const llvm::CallBase*> calls;
			for (const llvm::BasicBlock& block : function)
				for (const llvm::Instruction& instruction : block)
					if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
						if (call->getCalledFunction() && call->getCalledFunction()->getName() == callee) calls.push_back(call);
			return calls;
		}

		bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

		// Whether the script compiled; when it did not, its diagnostics go to the test's output.
		bool Compiled(const CompiledScript& script)
		{
			if (!script.module) std::cout << script.diagnostics << std::endl;
			return script.module != nullptr;
		}

		// The qualified names of the types the script's reflection image registers.
		std::vector<std::string> ImageTypes(const llvm::Module& module)
		{
			std::vector<std::string> names;
			const llvm::GlobalVariable* image = module.getNamedGlobal("__c4_reflect_image");
			if (!image || !image->hasInitializer()) return names;
			const auto* types = llvm::dyn_cast<llvm::GlobalVariable>(llvm::cast<llvm::ConstantStruct>(image->getInitializer())->getOperand(1));
			if (!types) return names;
			for (const llvm::Use& row : llvm::cast<llvm::ConstantArray>(types->getInitializer())->operands())
			{
				const auto* name = llvm::cast<llvm::GlobalVariable>(llvm::cast<llvm::ConstantStruct>(row)->getOperand(1));
				names.push_back(llvm::cast<llvm::ConstantDataArray>(name->getInitializer())->getAsCString().str());
			}
			return names;
		}

		bool Lists(const std::vector<std::string>& names, const char* name) { return std::find(names.begin(), names.end(), name) != names.end(); }

		// Whether an image-relative offset (`sub (ptrtoint f), (ptrtoint __ImageBase)`) names a function the module has
		// no code for.
		bool AnOffsetLeavesTheImage(const llvm::Module& module)
		{
			const llvm::GlobalVariable* base = module.getNamedGlobal("__ImageBase");
			if (!base) return false;
			for (const llvm::Function& f : module)
			{
				if (!f.isDeclaration() && !f.hasAvailableExternallyLinkage()) continue;
				for (const llvm::User* address : f.users())
					for (const llvm::User* use : address->users())
						if (const auto* offset = llvm::dyn_cast<llvm::ConstantExpr>(use); offset && offset->getOpcode() == llvm::Instruction::Sub)
							if (const auto* from = llvm::dyn_cast<llvm::ConstantExpr>(offset->getOperand(1)); from && from->getOperand(0) == base) return true;
			}
			return false;
		}

		bool HasImageThunk(const llvm::Module& module)
		{
			for (const llvm::Function& f : module)
				if (f.getName().starts_with("c4.image.") && !f.isDeclaration()) return true;
			return false;
		}
	}

	void ScriptCompilerTest::AScriptCallsItsHost()
	{
		if (!Host().index) std::cout << Host().diagnostics << std::endl;
		TEST_ASSERT(Host().index != nullptr);
		CompiledScript script = CompileTestScript(Host(), "include \"game.ch\"\nint32 Twice(int32 v) { return Game::Add(v, v); }\n");
		TEST_ASSERT(Compiled(script));
		TEST_ASSERT(Defines(*script.module, "Twice_1int32"));
		TEST_ASSERT(!Defines(*script.module, "Game_Add_2int32int32"));            // the host's code
		TEST_ASSERT(script.module->getNamedGlobal("__c4_script_module") != nullptr);
		TEST_ASSERT(script.module->getFunction("main") == nullptr);
		TEST_PASSED();
	}

	void ScriptCompilerTest::HostGlobalsAreTheHostsAndNotStarted()
	{
		CompiledScript script = CompileTestScript(Host(),
			"include \"game.ch\"\nint32 g_mine = Game::Add(1, 2);\nint32 Score() { return Game::g_score + g_mine; }\n");
		TEST_ASSERT(Compiled(script));
		const llvm::GlobalVariable* score = script.module->getNamedGlobal("Game_g_score");
		TEST_ASSERT(score != nullptr && score->isDeclaration());
		const llvm::GlobalVariable* mine = script.module->getNamedGlobal("g_mine");
		TEST_ASSERT(mine != nullptr && !mine->isDeclaration());
		TEST_ASSERT(Defines(*script.module, "__c4_script_init"));
		TEST_ASSERT(script.module->getNamedGlobal("llvm.global_ctors") == nullptr);  // the loader starts it
		for (const llvm::User* user : score->users())
			TEST_ASSERT(!llvm::isa<llvm::StoreInst>(user));                             // nothing in the script starts it
		TEST_PASSED();
	}

	void ScriptCompilerTest::HostTemplateInstancesAreTheHosts()
	{
		CompiledScript script = CompileTestScript(Host(),
			"include \"game.ch\"\nint32 FromBoxes() { Game::Box<int32> b; b.v = 3; Game::Box<float> f; f.v = 2.0f; return b.Get() + (int32)f.Get(); }\n");
		TEST_ASSERT(Compiled(script));
		const llvm::Function* hosts = script.module->getFunction("Game_Box_Tint32_E_Get_0");
		TEST_ASSERT(hosts != nullptr && (hosts->isDeclaration() || hosts->hasAvailableExternallyLinkage()));
		TEST_ASSERT(Defines(*script.module, "Game_Box_Tfloat_E_Get_0"));             // the host never made it
		TEST_PASSED();
	}

	void ScriptCompilerTest::AScriptDerivesFromHostClasses()
	{
		CompiledScript script = CompileTestScript(Host(),
			"include \"game.ch\"\n"
			"class Scout : Game::Entity { }\n"
			"class Grunt : Game::Entity { public override int32 Damage(int32 amount) { return 0; } }\n"
			"int32 Make() { Scout* s = new Scout(); Grunt* g = new Grunt(); return s->Damage(1) + g->Damage(1); }\n");
		TEST_ASSERT(Compiled(script));
		TEST_ASSERT(!Has(script.diagnostics, "abstract"));
		TEST_ASSERT(Defines(*script.module, "Grunt_Damage_1int32"));
		TEST_PASSED();
	}

	// BUG-243: the script's descriptor of a host class became the host's, which its image-relative RTTI cannot reach.
	void ScriptCompilerTest::TypeDescriptorsAreTheScriptsOwn()
	{
		CompiledScript script = CompileTestScript(Host(),
			"include \"game.ch\"\nclass Scout : Game::Entity { }\nGame::Entity* MakeScout() { return new Scout(); }\n");
		TEST_ASSERT(Compiled(script));
		const llvm::GlobalVariable* entity = script.module->getNamedGlobal("??_R0?AVEntity@Game@@@8");
		TEST_ASSERT(entity != nullptr && !entity->isDeclaration());
		TEST_PASSED();
	}

	// BUG-244: a host class's object the script made pointed at the script's copy of its vtable, gone at unload.
	void ScriptCompilerTest::ObjectsOfHostClassesUseTheHostsVtables()
	{
		CompiledScript script = CompileTestScript(Host(), "include \"game.ch\"\nGame::Entity* Make() { return new Game::Entity(); }\n");
		TEST_ASSERT(Compiled(script));
		size_t vtables = 0;
		for (const llvm::GlobalVariable& g : script.module->globals())
		{
			if (!g.getName().starts_with("__c4_vtable_Game") || !g.getName().contains("Entity")) continue;
			vtables++;
			TEST_ASSERT(g.isDeclaration());
		}
		TEST_ASSERT_EQ(vtables, (size_t)1);
		TEST_PASSED();
	}

	void ScriptCompilerTest::ExitsAndThrowsGoThroughTheScriptRuntime()
	{
		CompiledScript script = CompileTestScript(Host(),
			"struct Res { public int32 n; [operator(\"delete\")] public void Drop() { n = 0; } }\nRes g_res;\nvoid Fail() { throw 3; }\n");
		TEST_ASSERT(Compiled(script));
		TEST_ASSERT(script.module->getFunction("c4_at_exit") == nullptr);
		TEST_ASSERT(script.module->getFunction("c4_script_at_exit") != nullptr);
		TEST_ASSERT(script.module->getFunction("_CxxThrowException") == nullptr);
		TEST_ASSERT(Calls(*script.module->getFunction("Fail_0"), "c4_script_throw"));
		TEST_PASSED();
	}

	void ScriptCompilerTest::HostThreadLocalsGoThroughTheirAccessor()
	{
		CompiledScript script = CompileTestScript(Host(), "include \"game.ch\"\nint32 Frames() { return Game::t_frames; }\n");
		TEST_ASSERT(Compiled(script));
		TEST_ASSERT(Calls(*script.module->getFunction("Frames_0"), "__c4_tls_Game_t_frames"));
		TEST_ASSERT(script.module->getNamedGlobal("Game_t_frames") == nullptr);
		TEST_PASSED();
	}

	// BUG-242: the accessor call put in a catch had no funclet bundle, and WinEH preparation made it unreachable.
	void ScriptCompilerTest::HostThreadLocalsInACatchNameTheirFunclet()
	{
		CompiledScript script = CompileTestScript(Host(),
			"include \"game.ch\"\nint32 Caught() { try { throw 1; } catch (int32 e) { return Game::t_frames + e; } return 0; }\n");
		TEST_ASSERT(Compiled(script));
		const std::vector<const llvm::CallBase*> calls = CallsOf(*script.module->getFunction("Caught_0"), "__c4_tls_Game_t_frames");
		TEST_ASSERT_EQ(calls.size(), (size_t)1);
		TEST_ASSERT(calls[0]->getOperandBundle(llvm::LLVMContext::OB_funclet).has_value());
		TEST_PASSED();
	}

	void ScriptCompilerTest::AThreadLocalThatRunsCodeStartsOnFirstUse()
	{
		CompiledScript script = CompileTestScript(Host(),
			"int32 Seed() { return 5; }\nthread_local int32 t_seeded = Seed();\nthread_local int32 t_plain = 3;\n"
			"int32 Read() { return t_seeded + t_plain; }\n");
		TEST_ASSERT(Compiled(script));
		const llvm::Function* start = script.module->getFunction("__c4_tls_start_t_seeded");
		TEST_ASSERT(start != nullptr && !start->isDeclaration() && start->hasInternalLinkage());
		TEST_ASSERT(Calls(*start, "Seed_0"));
		TEST_ASSERT(!Calls(*start, "__c4_tls_start_t_seeded"));
		TEST_ASSERT(Calls(*script.module->getFunction("Read_0"), "__c4_tls_start_t_seeded"));
		TEST_ASSERT(script.module->getFunction("__c4_tls_start_t_plain") == nullptr);       // a constant start
		TEST_ASSERT(script.module->getFunction("__c4_script_init") == nullptr);             // nothing starts with the script
		TEST_PASSED();
	}

	void ScriptCompilerTest::AFirstUseStartThrowsToTheEnclosingHandler()
	{
		CompiledScript script = CompileTestScript(Host(),
			"int32 Fail() { throw 7; return 0; }\nthread_local int32 t_risky = Fail();\n"
			"int32 Guarded() { try { return t_risky; } catch (int32 e) { return e; } return 0; }\n");
		TEST_ASSERT(Compiled(script));
		const std::vector<const llvm::CallBase*> calls = CallsOf(*script.module->getFunction("Guarded_0"), "__c4_tls_start_t_risky");
		TEST_ASSERT_EQ(calls.size(), (size_t)1);
		TEST_ASSERT(llvm::isa<llvm::InvokeInst>(calls[0]));
		TEST_PASSED();
	}

	void ScriptCompilerTest::ReflectionHoldsOnlyTheScriptsOwn()
	{
		CompiledScript script = CompileTestScript(Host(),
			"include \"game.ch\"\nclass Grunt : Game::Entity { public override int32 Damage(int32 amount) { return 0; } }\n"
			"int32 Use() { Game::Box<int32> b; b.v = 1; Game::Box<int64> c; c.v = 2; return b.Get() + (int32)c.Get(); }\n");
		TEST_ASSERT(Compiled(script));
		const std::vector<std::string> types = ImageTypes(*script.module);
		TEST_ASSERT(Lists(types, "Grunt"));
		TEST_ASSERT(Lists(types, "Game::Box<int64>"));
		TEST_ASSERT(!Lists(types, "Game::Entity"));
		TEST_ASSERT(!Lists(types, "Game::Box<int32>"));
		TEST_PASSED();
	}

	void ScriptCompilerTest::HostBodiesAreInlined()
	{
		CompiledScript script = CompileTestScript(Host(), "include \"game.ch\"\nint32 Twice(int32 v) { return Game::Add(v, v); }\n", true);
		TEST_ASSERT(Compiled(script));
		TEST_ASSERT(!Calls(*script.module->getFunction("Twice_1int32"), "Game_Add_2int32int32"));
		const llvm::Function* add = script.module->getFunction("Game_Add_2int32int32");
		TEST_ASSERT(!add || add->isDeclaration()); // kept only for inlining, gone after
		TEST_PASSED();
	}

	void ScriptCompilerTest::ImportingCOrCppIsAnError()
	{
		CompiledScript script = CompileTestScript(Host(), "extern \"c\" include \"stdio.h\";\nint32 One() { return 1; }\n");
		TEST_ASSERT(script.module == nullptr);
		TEST_ASSERT(Has(script.diagnostics, "a script cannot import C or C++"));
		TEST_PASSED();
	}

	void ScriptCompilerTest::AnIncludeOffTheAllowListIsAnError()
	{
		CompiledScript script = CompileTestScript(Host(), "include \"game.ch\"\nint32 One() { return 1; }\n", false, { "engine/" });
		TEST_ASSERT(script.module == nullptr);
		TEST_ASSERT(Has(script.diagnostics, "may not include 'game.ch'"));
		CompiledScript allowed = CompileTestScript(Host(), "include \"game.ch\"\nint32 One() { return 1; }\n", false, { "game" });
		TEST_ASSERT(Compiled(allowed));
		TEST_PASSED();
	}

	void ScriptCompilerTest::DefiningAHostFunctionIsAnError()
	{
		CompiledScript script = CompileTestScript(Host(), "include \"game.ch\"\nnamespace Game { int32 Add(int32 a, int32 b) { return 0; } }\n");
		TEST_ASSERT(script.module == nullptr);
		TEST_ASSERT(Has(script.diagnostics, "a script cannot define 'Add': its host defines it"));
		TEST_PASSED();
	}

	void ScriptCompilerTest::ASyntaxErrorGivesDiagnosticsAndNoModule()
	{
		CompiledScript script = CompileTestScript(Host(), "int32 Broken( { return 1; }\n");
		TEST_ASSERT(script.module == nullptr);
		TEST_ASSERT(Has(script.diagnostics, "script.cf"));
		TEST_PASSED();
	}

	void ScriptCompilerTest::AMissingFileIsAnError()
	{
		ScriptCompileOptions options;
		options.name = "missing";
		options.files.push_back({ (std::filesystem::temp_directory_path() / "c4_script_test_no_such_folder" / "script.cf").string(), std::nullopt });
		std::string error;
		std::unique_ptr<llvm::TargetMachine> machine = ScriptTargetMachine(Host().index->target, &error);
		TEST_ASSERT(machine != nullptr);
		CompiledScript script = CompileScript(Host().interface, *Host().index, options, *machine);
		TEST_ASSERT(script.module == nullptr);
		TEST_ASSERT(Has(script.diagnostics, "cannot read the script file"));
		TEST_PASSED();
	}

	// BUG-315: a thrown host type's destructor is the host's code, which an offset from the script's base cannot reach:
	// the throw information names a thunk in the script's image instead.
	void ScriptCompilerTest::ImageOffsetsNameOnlyTheImage()
	{
		for (bool optimize : { false, true })
		{
			CompiledScript script = CompileTestScript(Host(),
				"include \"game.ch\"\nvoid Refuse(int32 code) { if (code != 0) throw Game::Failure(); }\n", optimize);
			TEST_ASSERT(Compiled(script));
			TEST_ASSERT(HasImageThunk(*script.module));
			TEST_ASSERT(!AnOffsetLeavesTheImage(*script.module));
		}
		TEST_PASSED();
	}

	// A script takes the host's inline bodies two calls deep (docs/runtime/04 section 3.3): First and Second inline
	// into it, Third stays a call into the host.
	void ScriptCompilerTest::InlineBodiesComeTwoCallsDeep()
	{
		CompiledScript script = CompileTestScript(Host(), "include \"game.ch\"\nint32 UsesFirst(int32 v) { return Game::First(v); }\n", true);
		TEST_ASSERT(Compiled(script));
		const llvm::Function* uses = script.module->getFunction("UsesFirst_1int32");
		TEST_ASSERT(uses != nullptr);
		TEST_ASSERT(!Calls(*uses, "Game_First_1int32"));
		TEST_ASSERT(!Calls(*uses, "Game_Second_1int32"));
		TEST_ASSERT(Calls(*uses, "Game_Third_1int32"));
		TEST_PASSED();
	}
} // namespace Testing

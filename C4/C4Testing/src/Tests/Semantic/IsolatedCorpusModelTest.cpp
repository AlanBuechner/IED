#include "IsolatedCorpusModelTest.h"
#include "Macro/MacroPreprocessor.h"
#include "Macro/PredefinedMacros.h"
#include "Backend/HIR/HIR.h" // LowerFunction (the corpus-lowering regression net)
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Json/JsonReader.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <vector>
#include <string>
namespace fs = std::filesystem;

namespace Testing
{
	using namespace Scripting;

	// Locate assets/IsolatedTest by walking up from the current directory (the test runner is
	// normally started at the repo root, but this keeps the test working from any CWD). Empty if
	// not found.
	static fs::path FindCorpusRoot()
	{
		fs::path cur = fs::current_path();
		for (int i = 0; i < 8 && !cur.empty(); ++i)
		{
			fs::path candidate = cur / "assets" / "IsolatedTest";
			if (fs::is_directory(candidate)) return candidate;
			if (!cur.has_parent_path() || cur.parent_path() == cur) break;
			cur = cur.parent_path();
		}
		return fs::path();
	}

	// Build the program model from one real .cf file on disk. Returns a pointer to the shared
	// program-wide model, or nullptr when the file is missing or the model fails to build. When
	// `outUnitId` is non-null it receives the registered source id (so tests can walk that unit's AST).
	// The repo's Bedrock umbrella, registered the same way C4.exe registers the copy next to itself, so a
	// corpus file that uses Bedrock types in a signature builds exactly as it does through the driver.
	static fs::path FindBedrock(const fs::path& corpusRoot)
	{
		return corpusRoot.parent_path().parent_path() / "Bedrock";
	}

	// The processor a case is built for: the -march / -m flags its ExpectedExit.json entry gives the runner (D-62).
	static CpuTarget TargetOf(const fs::path& corpusRoot, const fs::path& file)
	{
		std::ifstream manifestFile(corpusRoot / "ExpectedExit.json", std::ios::binary);
		const std::string body((std::istreambuf_iterator<char>(manifestFile)), std::istreambuf_iterator<char>());
		const std::string frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body; // the reader reads framed documents
		Json::Reader reader;
		reader.Append(frame.data(), frame.size());
		Json::Value manifest;
		if (!reader.TryReadDocument(manifest)) return CpuTarget();
		const std::string caseName = file.parent_path().lexically_relative(corpusRoot).generic_string();
		const Json::Value compileFlags = manifest.Member(caseName.c_str()).Member("compileFlags");
		std::vector<std::string> flags;
		for (uint32 i = 0; i < compileFlags.Size(); ++i)
			if (compileFlags.At(i).IsString() && CpuTarget::IsTargetFlag(std::string(compileFlags.At(i).GetString())))
				flags.push_back(std::string(compileFlags.At(i).GetString()));
		std::string error;
		return CpuTarget::FromFlags(flags, error).value_or(CpuTarget());
	}

	// As the driver does: a file's text is macro-expanded before it is parsed.
	static bool ExpandMacros(CompilerInstance& compiler, uint32 id, const std::string& fileName)
	{
		auto& sources = compiler.GetSourceManager();
		const Source& source = *sources.GetInfo(id).source.Get();
		MacroTable table;
		for (const auto& [name, value] : PredefinedMacros(false, compiler.GetProgramModel().Target(), {}))
			table.Define(name, value);
		PreprocessedFile expanded = MacroPreprocessor::Process(std::string(source.Data(), source.Size()), fileName, table, nullptr);
		if (!expanded.diagnostics.empty()) return false;
		sources.SetSourceText(id, expanded.text);
		return true;
	}

	// As the driver does for one input file: the headers beside it are the sources its includes reach.
	static bool RegisterHeadersBeside(CompilerInstance& compiler, const fs::path& file)
	{
		std::error_code ec;
		for (const auto& entry : fs::directory_iterator(file.parent_path(), ec))
		{
			if (entry.path().extension() != ".ch") continue;
			const uint32 id = compiler.RegisterFile(entry.path());
			if (id == 0 || !ExpandMacros(compiler, id, entry.path().filename().string())) return false;
		}
		return !ec;
	}

	static ProgramModel* BuildModelFromFile(CompilerInstance& compiler, const fs::path& file, uint32* outUnitId = nullptr)
	{
		if (!fs::is_regular_file(file)) return nullptr;
		compiler.SetTarget(TargetOf(FindCorpusRoot(), file));
		uint32 id = compiler.RegisterFile(file);
		if (id == 0) return nullptr;
		if (outUnitId) *outUnitId = id;
		if (!ExpandMacros(compiler, id, file.filename().string())) return nullptr;
		if (!RegisterHeadersBeside(compiler, file)) return nullptr;
		compiler.RegisterIncludedLibraryFiles(FindBedrock(FindCorpusRoot())); // as the driver does: opt-in by include
		compiler.CompileAll();
		bool ok = compiler.AnalyzeProgram();
		ProgramModel& shared = compiler.GetProgramModelForLowering();
		return (ok && shared.IsValid()) ? &shared : nullptr;
	}

	static std::string Diagnostics(CompilerInstance& compiler)
	{
		std::string all;
		for (const auto& e : compiler.GetAllDiagnostics())
			if (e.severity != ErrorSeverity::Warning) all += std::string(all.empty() ? "" : " | ") + e.message.c_str();
		return all.empty() ? "no diagnostic" : all;
	}

	// First AST node in a unit matching the predicate (depth-first). Lets a test pull a concrete
	// type/call node out of parsed source so it can be queried by its source-level spelling.
	template <typename Pred>
	static const AstNode* FindNode(CompilerInstance& compiler, uint32 unitId, Pred pred)
	{
		auto unit = compiler.GetCompilationUnit(unitId);
		if (!unit || !unit->GetRootAST()) return nullptr;
		const AstNode* found = nullptr;
		std::function<void(const AstNode&)> walk = [&](const AstNode& n) {
			if (found) return;
			if (pred(n)) { found = &n; return; }
			for (const auto& child : n.Children())
				walk(*child.Get());
		};
		walk(*unit->GetRootAST().Get());
		return found;
	}

	static const FunctionRecord* FindRec(ProgramModel& model, const char* name)
	{
		Core::Vector<const FunctionRecord*> recs;
		model.AllFunctionRecords(recs);
		for (const auto* r : recs)
			if (r->mangledName == name ||
				(r->mangledName.size() > strlen(name) &&
					r->mangledName.compare(0, strlen(name), name) == 0 &&
					r->mangledName[strlen(name)] == '_')) // mangled = Name_<overloadSuffix>
				return r;
		return nullptr;
	}

	// Does any lowering record carry the given operator annotation? (A method may carry several.)
	static bool AnyRecordHasOperator(ProgramModel& model, const char* annotation)
	{
		Core::Vector<const FunctionRecord*> recs;
		model.AllFunctionRecords(recs);
		for (const auto* r : recs)
			for (const auto& a : r->operatorAnnotations) if (a == annotation) return true;
		return false;
	}

	void IsolatedCorpusModelTest::RangeForFileProducesTypesAndBodies()
	{
		fs::path root = FindCorpusRoot();
		TEST_ASSERT(!root.empty()); // corpus must be present for this test to mean anything
		CompilerInstance compiler;
		ProgramModel* mp = BuildModelFromFile(compiler, root / "ControlFlow" / "RangeForBasic" / "RangeForTest.cf");
		TEST_ASSERT(mp != nullptr);
		ProgramModel& model = *mp;

		// The named struct resolves with a COMPUTED layout (two int32 fields).
		auto t = model.QueryType("Point");
		TEST_ASSERT(t.Ok());
		TEST_ASSERT_EQ((int)t.value->GetKind(), (int)TypeKind::Object);
		auto sz = model.SizeOf(t.value);
		TEST_ASSERT(sz.Ok());
		TEST_ASSERT_EQ((int)sz.value, 8);

		// Every function DEFINED in the file has a lowering record carrying its body node - the
		// exact fact a lower system needs to emit it.
		const char* defined[] = { "SumInts", "SumFloats", "StructFields", "BreakTest", "ContinueTest", "main" };
		for (const char* name : defined)
		{
			const FunctionRecord* r = FindRec(model, name);
			TEST_ASSERT(r != nullptr);
			TEST_ASSERT(r->bodyNode != nullptr); // definition, not declaration-only
		}

		// The [operator("create")] constructor is recorded as an operator annotation.
		TEST_ASSERT(AnyRecordHasOperator(model, "create"));
	}

	void IsolatedCorpusModelTest::OperatorFileRecordsOperatorAnnotations()
	{
		fs::path root = FindCorpusRoot();
		CompilerInstance compiler;
		ProgramModel* mp = BuildModelFromFile(compiler, root / "Operators" / "CustomOperatorDispatch" / "CustomOperatorTest.cf");
		TEST_ASSERT(mp != nullptr);
		ProgramModel& model = *mp;

		auto t = model.QueryType("A");
		TEST_ASSERT(t.Ok());

		// Each [operator(...)] annotation in the file is present on some lowering record.
		const char* ops[] = { "create", "hash", "scale", "double" };
		for (const char* op : ops)
			TEST_ASSERT(AnyRecordHasOperator(model, op));

		// The operator methods are queryable by name through the ONE function verb: a hash method
		// on A resolves to a record with a body.
		auto h = model.QueryFunction("operator_hash", t.value);
		TEST_ASSERT(h.Ok());
		const FunctionRecord* rec = model.RecordFor(h.value);
		TEST_ASSERT(rec != nullptr && rec->bodyNode != nullptr);
	}

	void IsolatedCorpusModelTest::InheritanceFileComputesLayoutAndVirtuals()
	{
		fs::path root = FindCorpusRoot();
		CompilerInstance compiler;
		ProgramModel* mp = BuildModelFromFile(compiler, root / "OOP" / "VirtualBaseDerived" / "VirtualBasic.cf");
		TEST_ASSERT(mp != nullptr);
		ProgramModel& model = *mp;

		auto base = model.QueryType("Shape");
		auto derived = model.QueryType("Circle");
		TEST_ASSERT(base.Ok() && derived.Ok());

		// Both compute a layout; the derived type is strictly larger than its base.
		auto sb = model.SizeOf(base.value);
		auto sd = model.SizeOf(derived.value);
		TEST_ASSERT(sb.Ok() && sd.Ok());
		TEST_ASSERT(sb.value > 0);
		TEST_ASSERT(sd.value > sb.value);

		// The virtual method is recorded as virtual, and every out-of-line definition attached its
		// body to the record.
		const FunctionRecord* shapeArea = FindRec(model, "Shape_GetArea");
		TEST_ASSERT(shapeArea != nullptr);
		TEST_ASSERT(shapeArea->isVirtual);
		TEST_ASSERT(shapeArea->bodyNode != nullptr); // out-of-line `float Shape::GetArea()` attached

		const FunctionRecord* circleArea = FindRec(model, "Circle_GetArea");
		TEST_ASSERT(circleArea != nullptr);
		TEST_ASSERT(circleArea->bodyNode != nullptr);

		// The free polymorphic dispatcher has a body too.
		const FunctionRecord* calc = FindRec(model, "CalculateArea");
		TEST_ASSERT(calc != nullptr && calc->bodyNode != nullptr);
	}

	void IsolatedCorpusModelTest::OutOfLineMethodAttachesBodyToDeclaration()
	{
		fs::path root = FindCorpusRoot();
		CompilerInstance compiler;
		ProgramModel* mp = BuildModelFromFile(compiler, root / "OOP" / "OutOfLineMethod" / "OutofLineTest.cf");
		TEST_ASSERT(mp != nullptr);
		ProgramModel& model = *mp;

		auto t = model.QueryType("A");
		TEST_ASSERT(t.Ok());

		// `void A::Set(int32 x)` is declared in the class and DEFINED out-of-line. The single
		// lowering record must carry that definition's body node - declaration-only would be null.
		const FunctionRecord* set = FindRec(model, "A_Set");
		TEST_ASSERT(set != nullptr);
		TEST_ASSERT(set->bodyNode != nullptr);
		TEST_ASSERT(set->containingType == t.value); // it is A's method (implicit this)

		// The field the out-of-line method writes is queryable with its layout offset.
		auto m = model.QueryMember(t.value, "v");
		TEST_ASSERT(m.Ok() && m.value.IsField);
	}

	void IsolatedCorpusModelTest::TemplateFileInstantiatesToConcreteLayouts()
	{
		fs::path root = FindCorpusRoot();
		CompilerInstance compiler;
		uint32 unitId = 0;
		ProgramModel* mp = BuildModelFromFile(compiler, root / "Templates" / "ObjectTemplateBasic" / "ObjectTemplateProbe.cf", &unitId);
		TEST_ASSERT(mp != nullptr);
		ProgramModel& model = *mp;

		// `Box<int32> b;` - the template spelling in source resolves to a CONCRETE type: naming it
		// transparently instantiates it, and from that moment it is an ordinary object type.
		const AstNode* varB = FindNode(compiler, unitId, [](const AstNode& n) {
			if (const auto* v = dynamic_cast<const VariableDefinitionNode*>(&n))
				return v->GetNameToken().GetText() == "b";
			return false;
		});
		TEST_ASSERT(varB != nullptr);
		auto boxInt = model.ResolveType(*dynamic_cast<const VariableDefinitionNode*>(varB)->GetType().Get());
		TEST_ASSERT(boxInt.Ok());
		TEST_ASSERT_EQ((int)boxInt.value->GetKind(), (int)TypeKind::Object);
		auto szInt = model.SizeOf(boxInt.value);
		TEST_ASSERT(szInt.Ok());
		TEST_ASSERT_EQ((int)szInt.value, 4); // one int32

		// `Box<uint64> u;` - a DIFFERENT instantiation: same template, distinct concrete type.
		const AstNode* varU = FindNode(compiler, unitId, [](const AstNode& n) {
			if (const auto* v = dynamic_cast<const VariableDefinitionNode*>(&n))
				return v->GetNameToken().GetText() == "u";
			return false;
		});
		TEST_ASSERT(varU != nullptr);
		auto boxU64 = model.ResolveType(*dynamic_cast<const VariableDefinitionNode*>(varU)->GetType().Get());
		TEST_ASSERT(boxU64.Ok());
		auto szU64 = model.SizeOf(boxU64.value);
		TEST_ASSERT(szU64.Ok());
		TEST_ASSERT_EQ((int)szU64.value, 8); // one uint64

		// The two instantiations are distinct types (different element sizes) and each carries its
		// own instantiated methods with bodies - exactly what lowering needs to emit them.
		TEST_ASSERT(boxInt.value != boxU64.value);
	}

	void IsolatedCorpusModelTest::NamespacedFileResolvesQualifiedTypesAndBodies()
	{
		fs::path root = FindCorpusRoot();
		CompilerInstance compiler;
		ProgramModel* mp = BuildModelFromFile(compiler, root / "Namespaces" / "NamespacedValueMath" / "NsValueMath.cf");
		TEST_ASSERT(mp != nullptr);
		ProgramModel& model = *mp;

		// A type and a function live in namespace NsMath. The qualified name resolves through the
		// ONE verb - no special namespace path - and both compute layout / carry bodies.
		auto vec3 = model.QueryType("NsMath::Vec3");
		TEST_ASSERT(vec3.Ok());
		TEST_ASSERT_EQ((int)vec3.value->GetKind(), (int)TypeKind::Object);
		auto sz = model.SizeOf(vec3.value);
		TEST_ASSERT(sz.Ok());
		TEST_ASSERT_EQ((int)sz.value, 12); // three floats

		auto mat4 = model.QueryType("NsMath::Mat4");
		TEST_ASSERT(mat4.Ok());
		auto sm = model.SizeOf(mat4.value);
		TEST_ASSERT(sm.Ok());
		TEST_ASSERT_EQ((int)sm.value, 48); // four Vec3 columns

		// A namespaced free function resolves and its out-of-line definition attached a body.
		auto add = model.QueryFunction("NsMath::Vec3_Add");
		TEST_ASSERT(add.Ok());
		const FunctionRecord* rec = model.RecordFor(add.value);
		TEST_ASSERT(rec != nullptr && rec->bodyNode != nullptr);
	}

	void IsolatedCorpusModelTest::EverySelfContainedFileBuildsACompleteModel()
	{
		fs::path root = FindCorpusRoot();
		TEST_ASSERT(!root.empty()); // corpus must be present for this test to mean anything

		// Files that are NOT self-contained on their own and therefore cannot build a complete
		// single-file model. Each is a legitimate limitation, not a model bug:
		//   Dx12DeviceInit        - needs a GPU/driver and external D3D headers (env-dependent)
		//   MacroDefineUndef      - spans multiple .ch files (ChainA/ChainB/MacroDefs), not one file
		//   NsTemplateAliasCrossFile - by design two .cf files + a shared header
		//   Win32WindowCallback   - declares a native Win32 callback ('WinProc') with no C4 definition
		//   ExtensionMethodCollision - COM interop; 'SetGuid'/'IidTest' are native externs
		//   CppExceptionsAndInterfaces - its interface signatures use types CBind generates from the C++ header
		//   CppDestructorRuns     - its C4 types hold a C++ class that CBind binds from the header
		//   SizeOfIncompleteType  - INTENTIONAL negative test: sizeof of a forward-declared type must be
		//                           a clear compile error (not a complete model), so it cannot build one here
		static const char* kNotSelfContained[] = {
			"Interop/Dx12DeviceInit/probe.cf",
			"Macros/MacroDefineUndef/MacroTest.cf",
			"Namespaces/NsTemplateAliasCrossFile/vecns.cf",
			"Interop/Win32WindowCallback/Win32Window.cf",
			"Interop/ExtensionMethodCollision/ExtMethodCollision.cf",
			"Interop/CppExceptionsAndInterfaces/main.cf",
			"Interop/CppDestructorRuns/main.cf",
			"Types/SizeOfIncompleteType/main.cf",
		};
		auto isException = [&root](const fs::path& rel) {
			for (const char* x : kNotSelfContained) if (rel == fs::path(x)) return true;
			return false;
		};

		int checked = 0, skipped = 0;
		std::vector<std::string> failures;
		std::error_code ec;
		for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
			if (!ec && it->is_regular_file() && it->path().extension() == ".cf")
			{
				fs::path rel = it->path().lexically_relative(root);
				if (isException(rel)) { skipped++; continue; }

				CompilerInstance compiler;
				ProgramModel* mp = BuildModelFromFile(compiler, it->path());
				checked++;
				if (mp == nullptr) failures.push_back(rel.string() + ": " + Diagnostics(compiler));
			}

		// The corpus must actually be present and non-trivially large for this gate to mean anything.
		TEST_ASSERT(checked > 100);
		for (const auto& f : failures)
			printf("INCOMPLETE MODEL: %s\n", f.c_str());
		TEST_ASSERT_EQ((int)failures.size(), 0); // every self-contained file yields a complete model
	}

	void IsolatedCorpusModelTest::EverySelfContainedFileLowersWithoutSilentFailures()
	{
		fs::path root = FindCorpusRoot();
		TEST_ASSERT(!root.empty());

		int checkedFiles = 0, lowered = 0, incomplete = 0;
		std::vector<std::string> silent; // file + function that failed with NO diagnostic
		std::error_code ec;
		for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
			if (!ec && it->is_regular_file() && it->path().extension() == ".cf")
			{
				CompilerInstance compiler;
				ProgramModel* mp = BuildModelFromFile(compiler, it->path());
				if (mp == nullptr) continue; // model gate covers these separately
				checkedFiles++;

				ErrorTracker errors;
				Core::Vector<const FunctionRecord*> recs;
				mp->AllFunctionRecords(recs);
				for (const FunctionRecord* r : recs)
					if (r->bodyNode && !r->mangledName.empty())
					{
						HIRFunction fn = LowerFunction(*mp, errors, r);
						if (fn.complete) { lowered++; continue; }
						incomplete++;
						if (errors.GetErrors().Count() == 0) // the invariant: a failure must say why
							silent.push_back(it->path().lexically_relative(root).string() + " :: " + r->mangledName);
					}
			}

		TEST_ASSERT(checkedFiles > 100); // corpus present and non-trivially large
		for (const auto& s : silent)
			printf("SILENT LOWERING FAILURE: %s\n", s.c_str());
		TEST_ASSERT_EQ((int)silent.size(), 0); // no function fails to lower without a diagnostic
	}
} // namespace Testing




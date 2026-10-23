#pragma once
#include "Model/ProgramModel.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Backend/CodeGen/FunctionEmitter.h"
#include "Backend/CodeGen/PartitionEmitter.h"
#include "Backend/CodeGen/ProgramPlacement.h"
#include "Backend/CodeGen/ParallelLowering.h"
#include "Backend/HIR/GlobalInit.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Compiler/ErrorTracker.h"
#include <llvm/IR/LLVMContext.h>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
#include <unordered_map>
#include <set>
#include <deque>
#include <chrono>
#include <future>
#include <mutex>

namespace Scripting
{
	/// The codegen driver (doc 06): lowers a ProgramModel to ONE llvm::Module. Owns the per-compile
	/// ModuleContext and the lowered HIR trees, then runs the doc-06 assembly order: (1) lower every type +
	/// fill structs, (2) forward-declare every function, (3) emit every body (+ nested functions), (4) verify.
	/// It reads the model only for layout facts and symbols - it never re-resolves names or overloads (that is
	/// HIR's job). Incomplete bodies (declarations with no definition) stay declarations; a complete tree is
	/// emitted, an incomplete one is skipped.
	struct TestHarnessOptions;

	/// What lowering a script needs of its host (docs/runtime/04 section 4.2).
	struct ScriptLowering
	{
		const std::unordered_set<std::string>* hostSymbols = nullptr; // the names of the host table's entries
		const std::unordered_set<std::string>* hostTypes = nullptr;   // the types the host's reflection registered
		uint32 interfaceUnits = 0;                                    // units 1..interfaceUnits are the host's files
	};

	class CodeGenDriver
	{
		/// The constructor priority that runs after every C and C++ static constructor.
		static constexpr int kLastConstructorPriority = 65535;
		/// Reflection registers before every static constructor, so the program's global starts can use it.
		static constexpr int kReflectionPriority = 65534;

	public:
		explicit CodeGenDriver(llvm::LLVMContext& ctx);

		/// A lowered body and the partition it is emitted into.
		struct Placed
		{
			const HIRFunction* tree = nullptr;
			unsigned partition = 0;
		};

		// Build the module from the model. Returns true on success (module verified). `entryName` names the
		// user function the generated main calls ("" = no entry point emitted, e.g. a library build). The model
		// is non-const: lowering registers function-local scopes and template bindings into it (doc 06). On
		// failure `errorMessage` (if given) receives the verifier's diagnostic so the caller can report WHY. With
		// `tests` (a -test build) a program that has tests gets the test harness as its main.
		bool Lower(ProgramModel& model, const Core::String& entryName, std::string* errorMessage = nullptr,
			const TestHarnessOptions* tests = nullptr);

		/// The located diagnostics of a failed HIR lowering (empty when lowering succeeded).
		const Core::Vector<CompilationError>& LoweringErrors() const { return m_LoweringErrors.GetErrors(); }

		/// Function bodies are lowered, and partitions emitted, on up to `threads` threads (ParallelLowering), with the
		/// result of doing it on one; one before Lower. How it went, for -ftime.
		void LowerOn(unsigned threads) { m_Lowering = ParallelLowering(threads); }
		const LoweringStats& LoweringStatistics() const { return m_Lowering.Stats(); }
		/// How long each step of Lower took after the types (-ftime): the bodies, the static state, reflection,
		/// imports, the finish.
		const std::vector<std::pair<const char*, double>>& StepSeconds() const { return m_StepSeconds; }

		llvm::Module* module() { return &m_MCtx.module(); } // owned by this driver until destruction or TakeModule
		/// The module Lower built, which the caller then owns (a script compile keeps it past the driver).
		std::unique_ptr<llvm::Module> TakeModule() { return m_MCtx.TakeModule(); }

		/// The functions that got an invoke thunk (D-50), for the reflection registration.
		const InvokeThunks& Thunks() const { return m_Thunks; }

		/// `-gc-stats`: before main, the program installs the GC's default statistics printer.
		void InstallGcStatsPrinter();

		/// `-g` (B28): the module Lower builds carries debug information (CodeView).
		void EnableDebugInfo() { m_DebugInfo = true; }

		/// `-fno-strict-aliasing` (D-64): loads and stores carry no alias tags, so any may touch any memory.
		void DisableStrictAliasing() { m_StrictAliasing = false; }

		/// `-reflect-tag`: the program's reflection registers under `tag` (a hot-reload source) instead of "".
		void SetReflectionTag(const Core::String& tag) { m_ReflectionTag = tag; }

		/// `--script-host` (docs/runtime/04 section 3.1): every class with virtual methods gets its vtables whether or not
		/// the program makes one, so a script's objects of the program's classes use the program's, which outlive it.
		void ShareVtables() { m_ShareVtables = true; }

		/// Lower leaves the module unverified: the build verifies each partition as it assembles it
		/// (ObjectSettings::verify), on its threads, and a partition an earlier build assembled needs no second look.
		void VerifyLater() { m_VerifyLater = true; }

		/// The program is emitted into `count` partitions (D-81), each function and global in the one ProgramPlacement
		/// gives it: partition 0 is module(), with the program's own code and data; every other is a module of its own
		/// in an LLVM context of its own. The partitions emit at once on LowerOn's threads. Each copies the small
		/// functions it calls from the others (ImportPlanner), only `inline` ones with `onlyAlwaysInline` (-O0). Before
		/// Lower.
		void SplitInto(unsigned count, bool onlyAlwaysInline);
		unsigned PartitionCount() const { return 1 + static_cast<unsigned>(m_Others.size()); }
		/// Partition `i`'s module (0: module()).
		llvm::Module& PartitionModule(unsigned i) { return Partition(i).module(); }
		/// A split program's reflection image is an object of its own (D-82), kept by this key, which changes with what
		/// the image is made of; empty for a program that is not split or registers nothing. It is a hash of every value
		/// of the image, worked out on a thread of its own from when Lower keeps the image: this waits for it.
		const std::string& ReflectionImageKey();
		/// That image built into a module of `context`, which defines `__c4_reflect_image` (partition 0 declares and
		/// registers it). It reads no model: any thread may build it, once Lower has returned.
		std::unique_ptr<llvm::Module> BuildReflectionModule(llvm::LLVMContext& context) const;

		/// Lower a script (docs/runtime/04 section 4.2), with no entry point: the host's definitions and storage win
		/// (Script/ScriptModulePasses), the host's globals are not started, the script's start from
		/// `__c4_script_init`, the reflection image registers through the loader, and exits and throws go through the
		/// script runtime.
		void EnableScriptMode(ScriptLowering script) { m_Script = script; }

	private:
		// The assembly steps of Lower, in order (docs/compiler/04-codegen-and-abi.md).
		void LowerTypes(ProgramModel& model);
		void EmitFileAssembly(const ProgramModel& model);
		void DeclareWrittenFunctions(const Core::Vector<const FunctionRecord*>& records);
		bool EmitEntryPoint(const Core::Vector<const FunctionRecord*>& records, const Core::String& entryName, std::string* errorMessage);
		void EmitMainWrapper(const FunctionRecord& entry);
		bool EmitStaticState(ProgramModel& model, std::string* errorMessage);
		/// The reflection image (Backend/Reflection/RegistrationEmitter) and `__c4_reflect_init`, which registers it
		/// before main. False, with the diagnostics, when an annotation argument does not fold.
		bool EmitReflection(ProgramModel& model, std::string* errorMessage);
		/// The image built into partition 0, sharing its strings.
		llvm::GlobalVariable* BuildImageHere(const ImageDescription& image);
		/// The image kept for a module of its own (BuildReflectionModule), and its key; partition 0's declaration of it.
		llvm::GlobalVariable* KeepImageApart(ImageDescription image);
		void FinishModule(ProgramModel& model);
		bool Verify(std::string* errorMessage);

		/// Lowers and emits `batch`, then every template-made or compiler-written function the emitted code
		/// references for the first time, until none is left (D-16). False, with the diagnostics, when a body
		/// fails to lower.
		bool EmitReachable(ProgramModel& model, Core::Vector<const FunctionRecord*> batch, std::string* errorMessage);
		void NoteUse(const ProgramModel& model, const FunctionRecord& rec, ErrorTracker& errors);
		/// A lowered body joins the program: its diagnostics after the ones before it, its tree in `lowered`.
		void Keep(const ProgramModel& model, LoweredBody body, std::vector<Placed>& lowered);
		void LowerCensusFunctions(ProgramModel& model, std::vector<Placed>& lowered);
		/// `__c4_global_init`, registered to run before main: it runs the rounds of Backend/HIR/GlobalInit in
		/// order, each emitted with the functions it reaches, then attaches the main thread. `__c4_thread_start`
		/// runs the thread_local rounds on a thread that attaches. False, with the diagnostics, when a start or a
		/// body it reaches fails to lower.
		bool EmitGlobalInit(ProgramModel& model, std::string* errorMessage);
		/// The rounds of one storage duration: how a round lowers, and what its functions are named.
		struct InitRounds
		{
			bool (*lower)(ProgramModel&, ErrorTracker&, HIRFunction&, StartedGlobals&, std::vector<ConstantStart>&);
			std::string prefix;
			std::vector<llvm::Function*> functions;
		};
		/// Lowers and emits the next round of `rounds` when a global of its kind is left to start; a global whose start
		/// is constant data starts holding it instead.
		bool EmitInitRound(ProgramModel& model, InitRounds& rounds, StartedGlobals& started, std::string* errorMessage);
		/// Gives each global of `constants` its data as its initial value (read-only when it is const); one whose
		/// storage's type cannot hold it as data starts in `round` after all.
		void StartAsData(std::vector<ConstantStart>& constants, HIRFunction& round);
		/// A program's thread_locals start in `rounds` when a thread attaches; a script's that run code start on their
		/// first use on each thread (docs/runtime/04 section 2).
		bool EmitThreadStarts(ProgramModel& model, InitRounds& rounds, StartedGlobals& started, std::string* errorMessage);
		bool EmitFirstUseStarts(ProgramModel& model, StartedGlobals& started, std::string* errorMessage);
		/// Each start code called is defined: the model's thread_locals and the starts lowered agree.
		bool EveryFirstUseStartDefined(std::string* errorMessage) const;
		/// `void name()` running `body`, a start the compiler wrote.
		llvm::Function* EmitStartFunction(ProgramModel& model, std::unique_ptr<HIRFunction> body, const std::string& name);
		/// An invoke thunk for every function reflection registers and can call that has none yet, then every
		/// function the thunks reach. A function an argument cannot be passed to gets no thunk.
		bool EmitInvokeThunks(ProgramModel& model, std::string* errorMessage);
		/// A thunk to lower: its record, the target it is lowered from, and whether it is its signature's shared one
		/// (calling the code it is given).
		struct ThunkJob
		{
			const FunctionRecord* target = nullptr;
			FunctionRecord* thunk = nullptr;
			bool shared = false;
		};
		/// The thunk a target calls through: its signature's shared one or its own, and its symbol.
		struct TargetThunk
		{
			bool shared = false;
			std::string symbol;
		};
		std::vector<TargetThunk> ThunksWanted(const ProgramModel& model, const Core::Vector<const FunctionRecord*>& targets);
		std::vector<ThunkJob> PlanThunks(ProgramModel& model, const Core::Vector<const FunctionRecord*>& targets,
			const std::vector<TargetThunk>& wanted);
		void FinishThunks(const std::vector<Placed>& lowered);
		llvm::Function* FinishThunk(const FunctionRecord& thunk, unsigned partition);
		void RecordThunks(const ProgramModel& model, const std::vector<std::pair<const FunctionRecord*, const FunctionRecord*>>& thunkOf);
		InvokeEntry InvokeEntryOf(const ProgramModel& model, const FunctionRecord& target, const FunctionRecord& thunk);
		/// The code reflection records as `target`'s address; none for a function without code (a pure virtual method).
		llvm::Function* CodeOf(const FunctionRecord& target, ModuleContext& home);
		/// Reflected functions with code to call that have not been considered for a thunk yet: a body emitted
		/// here, or a virtual method (the thunk dispatches through the object).
		Core::Vector<const FunctionRecord*> NewInvokeTargets(ProgramModel& model);
		bool HasCallableCode(const ProgramModel& model, const FunctionRecord& rec);
		/// The lowering failures so far as one message; false when there are none.
		bool LoweringFailed(std::string* errorMessage) const;
		void RegisterGlobalInit(const std::vector<llvm::Function*>& rounds);
		void RegisterThreadStart(const std::vector<llvm::Function*>& rounds);
		/// A `void name()` calling each of `calls`, then `last` (when there is one).
		llvm::Function* CallingInOrder(const char* name, const std::vector<llvm::Function*>& calls, llvm::Function* last);
		/// The vtables of every class with virtual methods (ShareVtables), then the functions they reach.
		bool EmitEveryVtable(ProgramModel& model, std::string* errorMessage);
		/// The storage of every global and static member, whether or not code names it: reflection registers each.
		void MaterializeGlobals(ProgramModel& model);
		/// A script's host has `symbol`.
		bool IsHostSymbol(const std::string& symbol) const;
		/// In a script, storage the host has: declared in an interface file or named by the host table.
		bool IsHostStorage(const SymbolInfo& sym) const;
		/// In a script, every host global counts as started: its start ran in the host.
		void MarkHostGlobalsStarted(ProgramModel& model, StartedGlobals& started) const;
		/// The script-mode passes over the finished module (Script/ScriptModulePasses).
		void ApplyScriptLowering();

	private:
		ModuleContext& Partition(unsigned i) { return i == 0 ? m_MCtx : m_Others[i - 1]->mctx(); }
		std::vector<llvm::Module*> Modules();
		FunctionEmitter& EmitterOf(unsigned i) { return i == 0 ? m_Emitter : m_Others[i - 1]->emitter(); }
		unsigned PartitionOf(const FunctionRecord& rec) const { return m_Placement ? m_Placement->Of(rec) : 0; }
		unsigned PartitionOf(const SymbolInfo& global) const { return m_Placement ? m_Placement->Of(global) : 0; }
		void TakeNewFunctions(Core::Vector<const FunctionRecord*>& batch);
		void EmitPlaced(const ProgramModel& model, const std::vector<Placed>& lowered, Core::Vector<const FunctionRecord*>& batch);
		/// The static locals every other partition defines, declared in partition 0 for its GC roots.
		void DeclareOthersStaticStorage();
		/// Records `step` as taking from `since` to now, and moves `since` to now.
		void Timed(const char* step, std::chrono::steady_clock::time_point& since);
		/// Every object type laid out before partitions read layouts at once (a type laid out on its first query).
		void SettleLayouts(const ProgramModel& model) const;
		/// Each partition's copies of the small functions it calls from the others, then what they reach.
		bool EmitImports(ProgramModel& model, std::string* errorMessage);
		std::vector<std::vector<const FunctionRecord*>> PlanImports();

		llvm::LLVMContext& m_Context;
		ModuleContext m_MCtx;
		std::vector<std::unique_ptr<HIRFunction>> m_Trees; // the lowered bodies, alive for the whole emit
		FunctionEmitter m_Emitter;
		ErrorTracker m_LoweringErrors;
		ParallelLowering m_Lowering{ 1 };
		std::vector<std::pair<const char*, double>> m_StepSeconds;
		bool m_DebugInfo = false;
		bool m_ShareVtables = false;
		bool m_VerifyLater = false;
		bool m_StrictAliasing = true;
		std::vector<std::unique_ptr<PartitionEmitter>> m_Others; // partitions 1..n-1 of a split program
		bool m_OnlyAlwaysInline = false;                         // -O0: a partition copies only `inline` functions
		std::unordered_map<const FunctionRecord*, const HIRFunction*> m_TreeOf; // the lowered body of each record
		std::mutex m_ModelLock;                                  // what emission asks of the model that can change it
		std::optional<ProgramPlacement> m_Placement;            // where a split program's functions and globals go
		std::set<const FunctionRecord*> m_Defined;              // lowered completely and emitted
		Core::String m_ReflectionTag;
		std::optional<ScriptLowering> m_Script;
		std::set<const FunctionRecord*> m_Emitted; // lowered (a failed one too: no retry)
		std::unordered_map<const FunctionRecord*, const HIRFunction*> m_FirstUsers; // whose emitted code first referenced it
		std::deque<FunctionRecord> m_GlobalInitRounds; // compiler-written: no model record
		std::vector<llvm::Function*> m_FirstUseStarts; // the starts of a script's thread_locals that start on first use
		std::deque<FunctionRecord> m_ThunkRecords;     // compiler-written: no model record
		std::set<const FunctionRecord*> m_ThunkTargets; // considered for a thunk (one that failed too: no retry)
		uint32 m_RecordsScanned = 0;                     // the model's records NewInvokeTargets has looked at
		std::vector<const FunctionRecord*> m_InvokeCandidates; // reflected and callable, without code to call yet
		std::unordered_map<std::string, const FunctionRecord*> m_SharedThunks; // by symbol; null for a signature that cannot be passed
		std::unordered_map<const FunctionRecord*, llvm::Function*> m_ThunkFunctions; // each thunk emitted, in its partition
		InvokeThunks m_Thunks;
		ImageDescription m_Image; // a split program's reflection image (D-82)
		std::string m_ImageKey;
		std::future<std::string> m_ImageKeying; // the key while its thread works it out
		std::string m_ImageTriple;
		std::string m_ImageLayout;
	};
} // namespace Scripting

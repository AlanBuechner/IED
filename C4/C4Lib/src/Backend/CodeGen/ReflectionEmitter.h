#pragma once
#include "Backend/CodeGen/PartitionSet.h"
#include "Backend/Lowering/LoweredProgram.h"
#include "Backend/Lowering/ScriptLowering.h"
#include "Backend/Reflection/ImageDescription.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Compiler/ErrorTracker.h"
#include <future>
#include <memory>
#include <string>
#include <unordered_map>

namespace Scripting
{
	/// The program's reflection (docs/runtime/02): its thunks made locals that only the image names, the image of every
	/// type, function and global it registers, and `__c4_reflect_init`, which registers it under the program's tag before
	/// every other static constructor. A split program's image is a module of its own (D-82), built only when the object
	/// cache keeps none of its key.
	class ReflectionEmitter
	{
	public:
		explicit ReflectionEmitter(PartitionSet& partitions) : m_Partitions(partitions) {}

		/// A script's image leaves out what its host registered, and its loader registers it under the script's tag.
		void ForScript(const ScriptLowering* script) { m_Script = script; }

		/// Each thunk `program` has, emitted already, made a small local of the program in its partition: only the image
		/// names it, and reflection calls through it seldom.
		void FinishThunks(const LoweredProgram& program, WorkerPool& pool);
		/// The image (registered under `tag`) and what registers it; a script's loader registers a script's. False, with
		/// the diagnostics in `errors`, when an annotation argument does not fold.
		bool Emit(ProgramModel& model, LoweredProgram& program, const Core::String& tag, ErrorTracker& errors, WorkerPool& pool);

		/// A split program's image's key, which changes with what the image is made of; empty for a program that is not
		/// split or registers nothing. It is worked out on a thread of its own: this waits for it.
		const std::string& ImageKey();
		/// That image built into a module of `context`, which defines `__c4_reflect_image`. It reads no model: any thread
		/// may build it, once the program is emitted.
		std::unique_ptr<llvm::Module> BuildImageModule(llvm::LLVMContext& context) const;

	private:
		llvm::Function* FinishThunk(const FunctionRecord& thunk, unsigned partition);
		/// How reflection calls each function, its entry made in the target's own partition, the partitions at once.
		InvokeThunks InvokeEntries(const LoweredProgram& program, WorkerPool& pool);
		InvokeEntry InvokeEntryOf(const InvokeThunkUse& use);
		TypeThunks TypeEntries(ProgramModel& model, LoweredProgram& program) const;
		llvm::Function* ThunkFunction(const FunctionRecord* thunk) const;
		/// The image built into partition 0, sharing its strings.
		llvm::GlobalVariable* BuildImageHere(const ImageDescription& image);
		/// The image kept for a module of its own, and its key; partition 0's declaration of it.
		llvm::GlobalVariable* KeepImageApart(ImageDescription image);
		void RegisterImage(llvm::GlobalVariable* image, const Core::String& tag);

		PartitionSet& m_Partitions;
		const ScriptLowering* m_Script = nullptr;
		std::unordered_map<const FunctionRecord*, llvm::Function*> m_ThunkFunctions; // each thunk emitted, in its partition
		ImageDescription m_Image;              // a split program's image (D-82)
		std::string m_ImageKey;
		std::future<std::string> m_ImageKeying; // the key while its thread works it out
		std::string m_ImageTriple;
		std::string m_ImageLayout;
	};
} // namespace Scripting

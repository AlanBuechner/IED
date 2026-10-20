#pragma once
#include "Backend/CodeGen/RttiEmitter.h"
#include "Backend/HIR/HIRNodes.h"
#include <llvm/IR/Module.h>
#include <functional>
#include <unordered_map>

namespace Scripting
{
	/// The MSVC EH tables behind throw and catch (D-13, docs/compiler/04). A thrown type's ThrowInfo lists
	/// every type a handler may catch it as: the type itself and each unambiguous base (with the offset of
	/// that sub-object), and for a pointer also each base pointer and `void*`. The frame handler picks the
	/// first clause whose TypeDescriptor matches one of them.
	class EhTables
	{
	public:
		EhTables(llvm::Module& module, RttiEmitter& rtti, std::function<llvm::Function*(const FunctionRecord&)> functionFor)
			: m_Module(module), m_Rtti(rtti), m_FunctionFor(std::move(functionFor)) {}

		/// The lifecycle the tables run on the exception object (ThrowNode::destroy / copies).
		struct ObjectLifecycle
		{
			const FunctionRecord* destroy = nullptr;
			std::vector<std::pair<const ObjectType*, const FunctionRecord*>> copies;
		};
		llvm::GlobalVariable* ThrowInfo(const Type* thrown, const ObjectLifecycle& lifecycle);

		/// The first two catchpad operands of a clause: its TypeDescriptor (null for `catch (...)`) and its
		/// adjectives (by reference, const, catch-all).
		struct Handler { llvm::Constant* typeDescriptor = nullptr; uint32_t adjectives = 0; };
		Handler HandlerFor(const TryNode::CatchClause& clause);

	private:
		struct Catchable { std::string name; int64 offset = 0; uint32_t size = 0; bool simple = false; llvm::Function* copy = nullptr; };
		std::vector<Catchable> CatchableTypes(const Type* thrown, const ObjectLifecycle& lifecycle) const;
		llvm::Constant* CatchableType(const Catchable& c);
		llvm::Constant* FunctionOffset(llvm::Function* fn);

		llvm::Module& m_Module;
		RttiEmitter& m_Rtti;
		std::function<llvm::Function*(const FunctionRecord&)> m_FunctionFor;
		std::unordered_map<const Type*, llvm::GlobalVariable*> m_ThrowInfos;
	};
} // namespace Scripting

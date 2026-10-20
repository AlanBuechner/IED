#include "Backend/CodeGen/PeTables.h"

namespace Scripting
{
	namespace PeTables
	{
		namespace
		{
			// One __ImageBase per module: several distinct "__ImageBase.N" symbols break the image-relative
			// relocations ("symbol '__ImageBase.N' can not be undefined in a subtraction").
			llvm::GlobalVariable* ImageBase(llvm::Module& m)
			{
				auto* base = m.getGlobalVariable("__ImageBase");
				if (base) return base;
				base = new llvm::GlobalVariable(m, llvm::Type::getInt8Ty(m.getContext()), false,
					llvm::GlobalValue::ExternalLinkage, nullptr, "__ImageBase");
				base->setDSOLocal(true);
				return base;
			}
		}

		llvm::Constant* ImageRelative(llvm::Module& m, llvm::GlobalValue* g)
		{
			llvm::LLVMContext& ctx = m.getContext();
			auto* i64 = llvm::Type::getInt64Ty(ctx);
			auto* target = llvm::ConstantExpr::getPtrToInt(g, i64);
			auto* base = llvm::ConstantExpr::getPtrToInt(ImageBase(m), i64);
			return llvm::ConstantExpr::getTrunc(llvm::ConstantExpr::getSub(target, base), llvm::Type::getInt32Ty(ctx));
		}

		llvm::GlobalVariable* Shared(llvm::Module& m, const std::string& name, llvm::Constant* init, const char* section)
		{
			if (auto* existing = m.getGlobalVariable(name)) return existing;
			auto* g = new llvm::GlobalVariable(m, init->getType(), true, llvm::GlobalValue::LinkOnceODRLinkage, init, name);
			if (section) g->setSection(section);
			g->setComdat(m.getOrInsertComdat(name));
			return g;
		}

		llvm::GlobalVariable* Private(llvm::Module& m, const std::string& name, llvm::Constant* init)
		{
			return new llvm::GlobalVariable(m, init->getType(), true, llvm::GlobalValue::PrivateLinkage, init, name);
		}

		llvm::Constant* I32(llvm::LLVMContext& ctx, int64_t v)
		{
			return llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), static_cast<uint64_t>(v), true);
		}
	}
} // namespace Scripting

#include "Backend/HIR/AssertionSites.h"
#include <cstring>
#include <utility>

namespace Scripting
{
	namespace
	{
		const char* LocatedForm(const FunctionRecord* callee)
		{
			static const std::pair<const char*, const char*> kForms[] = {
				{ "assert_true", "c4_assert_true_at" },
				{ "assert_eq", "c4_assert_eq_at" },
				{ "assert_feq", "c4_assert_feq_at" },
				{ "assert_se", "c4_assert_se_at" },
			};
			if (!callee || !callee->isBuiltin) return nullptr;
			for (const auto& [plain, located] : kForms)
				if (callee->mangledName == plain) return located;
			return nullptr;
		}
	}

	void LocateAssertion(LoweringContext& ctx, CallNode* call, const AstNode* site)
	{
		const char* form = LocatedForm(call->callee);
		if (!form || !site) return;
		const SymbolInfo* located = ctx.queries().Model()->Symbols()->GetRoot()->Find(form);
		if (!located || located->functions.Empty()) return;
		TypeStore* ts = ctx.queries().TypeStore();
		const Core::String file = ctx.queries().Model()->SourceName(site->startLocation.sourceId);
		call->callee = located->functions[0];
		call->args.push_back(ctx.out->NewNode<ConstantNode>(ts->Pointer(ts->Builtin(TypeKind::Void)), ConstantValue::String(std::string(file.c_str()), 1)));
		call->args.push_back(ctx.out->NewNode<ConstantNode>(ts->Builtin(TypeKind::Int32), ConstantValue::Int((int64)site->startLocation.line)));
	}
}

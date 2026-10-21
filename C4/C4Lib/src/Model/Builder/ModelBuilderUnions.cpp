#include "Model/Builder/ModelBuilder.h"
#include "Model/Builder/ObjectIdentity.h"
#include "Model/Builder/ModelResolver.h" // pass 2: fixed-point resolution + GAP A body scan
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/SwizzleViewNode.h"
#include "Model/ConstEvaluator.h"
#include "Model/OperatorAnnotations.h" // [group("name")] collection for reflection (plan 14)
#include <functional>

namespace Scripting
{
	void ModelBuilder::RegisterUnion(Context& ctx, const UnionDefinitionNode& un)
	{
		const Core::String& name = un.GetNameToken().GetText();

		UnionType* raw = new UnionType(name);
		ctx.model->m_Types->Own(raw);
		raw->SetDeclNode(&un);
		raw->SetDeclScope(ctx.scope);
		raw->SetEnclosingNames(EnclosingNames(ctx.scope));
		Core::Vector<Core::String> groups;
		for (const auto& annotation : un.GetAnnotations()) CollectGroupAnnotations(annotation.Get(), groups);
		raw->SetGroups(groups);
		ctx.model->m_UnionTypes.Push(raw);

		SymbolInfo info;
		info.kind = SymbolKind::Type;
		info.type = raw;
		info.definingUnit = ctx.unitId;
		info.declNode = &un;
		if (!ctx.scope->Register(name, info)) ErrorAt(ctx, &un, Redefinition(ctx, "union", name));

		ReportUnionMembersItCannotHold(ctx, un);
		m_PendingUnions.Push({ raw, &un, ctx.unitId, ctx.scope });
	}

	// A named union holds fields and struct views (BUG-192): anything else is an error where it is written.
	void ModelBuilder::ReportUnionMembersItCannotHold(const Context& ctx, const UnionDefinitionNode& un)
	{
		const Core::String name = un.GetNameToken().GetText();
		for (const auto& member : un.GetMembers())
		{
			const AstNode* node = member.Get();
			if (dynamic_cast<const SwizzleViewNode*>(node))
				ErrorAt(ctx, node, "a lane view ('packed float x, y;') goes in an anonymous union inside a class; the union '" + name +
					"' holds fields and struct views");
			else if (dynamic_cast<const UnionDefinitionNode*>(node))
				ErrorAt(ctx, node, "a union inside the union '" + name + "' is not supported: declare it beside '" + name + "', or use a struct view");
			else if (const auto* var = dynamic_cast<const VariableDefinitionNode*>(node); var && var->IsStatic())
				ErrorAt(ctx, node, "the union '" + name + "' cannot have a static member: declare '" + var->GetNameToken().GetText() + "' beside it");
		}
	}

	void ModelBuilder::ReportUnresolvedUnion(const Context& ctx, const PendingUnion& u)
	{
		const Core::String name = u.un->GetNameToken().GetText();
		bool reported = false;
		ForEachUnionField(*u.un, [&](const VariableDefinitionNode& field, int) {
			if (reported) return;
			const Core::String why = Why(ctx, field.GetType().Get(), u.unitId, u.scope);
			if (why.empty()) return;
			ErrorAt(ctx, &field, "unresolved type for field '" + field.GetNameToken().GetText() + "' in the union '" + name + "'" + why);
			reported = true;
		});
		if (!reported) ErrorAt(ctx, u.un, "unresolved union '" + name + "'");
	}

	void ForEachUnionField(const UnionDefinitionNode& un, const std::function<void(const VariableDefinitionNode&, int view)>& visit)
	{
		int nextView = 0;
		for (const auto& member : un.GetMembers())
		{
			if (const auto* var = dynamic_cast<const VariableDefinitionNode*>(member.Get()))
			{
				if (!var->IsStatic()) visit(*var, -1);
				continue;
			}
			const auto* view = dynamic_cast<const ObjectDefinitionNode*>(member.Get());
			if (!view) continue;
			const int id = nextView++;
			for (const auto& inner : view->GetBodyMembers())
				if (const auto* field = dynamic_cast<const VariableDefinitionNode*>(inner.Get())) visit(*field, id);
		}
	}

	// Flatten an anonymous union (swizzle) into the parent object's pending fields. Each plain lane
	// (`float x;`) becomes a region field at intra offset 0 (all alias); each struct view
	// (`struct { float x; ... }`) becomes a group of sequential lanes sharing one viewId. The geometry
	// (intra offsets, region size/align, first/last) is computed in ObjectType::CalculateLayout.
	void ModelBuilder::FlattenSwizzle(Context& ctx, const ObjectType* obj, const UnionDefinitionNode& un, int regionId)
	{
		int nextViewId = 0;
		for (const auto& member : un.GetMembers())
		{
			const auto* child = member.Get();
			if (!child) continue;
			if (const auto* vd = dynamic_cast<const VariableDefinitionNode*>(child))
				m_PendingFields.Push({ obj, vd, ctx.unitId, regionId, -1 });       // plain lane: aliases offset 0
			else if (const auto* od = dynamic_cast<const ObjectDefinitionNode*>(child))
				FlattenSwizzleView(ctx, obj, *od, regionId, nextViewId++);          // struct view: sequential lanes
			else if (const auto* sv = dynamic_cast<const SwizzleViewNode*>(child))
				m_PendingSwizzles.Push({ obj, sv, ctx.unitId });                    // packed/unpacked lane view -> SIMD region
		}
	}

	void ModelBuilder::FlattenSwizzleView(Context& ctx, const ObjectType* obj, const ObjectDefinitionNode& view, int regionId, int viewId)
	{
		for (const auto& member : view.GetBodyMembers())
		{
			const auto* child = member.Get();
			if (!child) continue;
			if (const auto* vd = dynamic_cast<const VariableDefinitionNode*>(child))
				m_PendingFields.Push({ obj, vd, ctx.unitId, regionId, viewId });    // a lane within this view
		}
	}

	// =====================================================================
} // namespace Scripting

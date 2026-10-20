#include "Backend/HIR/LambdaCaptures.h"
#include "Parser/Nodes/Statements/DestructuringNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Statements/ForLoopNode.h"
#include "Parser/Nodes/Statements/TryCatchNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <set>
#include <string>
#include <vector>

namespace Scripting
{
	namespace
	{
		constexpr const char* kThis = "this";

		class OuterUseWalker
		{
		public:
			explicit OuterUseWalker(OuterUses& out) : m_Out(out) {}

			void Lambda(const LambdaNode& lambda)
			{
				Push();
				for (const auto& p : lambda.GetParameters())
					if (p->HasName()) Declare(p->GetNameToken().GetText());
				for (const LambdaCapture& c : lambda.GetCaptures())
					if (c.kind == LambdaCapture::Kind::Init) Declare(c.name.GetText());
				Walk(lambda.GetBody().Get());
				Pop();
			}

		private:
			void Walk(const AstNode* node)
			{
				if (!node) return;
				switch (node->Type())
				{
				case NodeType::IdentifierExpression: Use(static_cast<const IdentifierExpressionNode*>(node)->GetName()); return;
				case NodeType::MemberAccess: Walk(static_cast<const MemberAccessNode*>(node)->GetBaseObject().Get()); return;
				case NodeType::VariableDefinition: Variable(*static_cast<const VariableDefinitionNode*>(node)); return;
				case NodeType::Destructuring: Destructured(*static_cast<const DestructuringNode*>(node)); return;
				case NodeType::CodeBlock: Scoped(node); return;
				case NodeType::ForLoop: Loop(*static_cast<const ForLoopNode*>(node)); return;
				case NodeType::CatchClause: Catch(*static_cast<const CatchClauseNode*>(node)); return;
				case NodeType::LambdaExpression: Nested(*static_cast<const LambdaNode*>(node)); return;
				case NodeType::FunctionDefinition: Declare(static_cast<const FunctionDefinitionNode*>(node)->GetName()->ToString()); return;
				case NodeType::StaticAccess: case NodeType::NamespaceQualifiedName: case NodeType::QualType:
				case NodeType::FunPtrType: case NodeType::TemplateArgumentList: case NodeType::ObjectDefinition:
				case NodeType::EnumDefinition: case NodeType::UnionDefinition: case NodeType::AliasDefinition:
				case NodeType::GotoStatement: case NodeType::LabelStatement: case NodeType::Annotation:
					return; // names here are types, qualified names or labels, never enclosing locals
				default:
					for (const auto& child : node->Children()) Walk(child.Get());
				}
			}

			void Variable(const VariableDefinitionNode& v)
			{
				for (const auto& d : v.GetDeclarators())
				{
					Walk(d.m_Initializer.Get());
					for (const auto& arg : d.m_CtorArgs) Walk(arg.Get());
					Declare(d.m_NameToken.GetText());
				}
			}

			void Destructured(const DestructuringNode& d)
			{
				Walk(d.GetValue().Get());
				for (const auto& e : d.GetElements())
				{
					if (e.target) Walk(e.target.Get());
					if (e.kind == DestructuringNode::Element::Kind::Declare) Declare(e.name.GetText());
				}
			}

			void Scoped(const AstNode* block)
			{
				Push();
				for (const auto& child : block->Children()) Walk(child.Get());
				Pop();
			}

			void Loop(const ForLoopNode& loop)
			{
				Push();
				if (loop.IsRangeBased())
				{
					Walk(loop.GetRangeExpression().Get());
					Declare(loop.GetRangeVarNameToken().GetText());
				}
				else
				{
					Walk(loop.GetInitClause().Get());
					Walk(loop.GetCondition().Get());
					for (const auto& step : loop.GetSteps()) Walk(step.Get());
				}
				Walk(loop.GetBody().Get());
				Walk(loop.GetSingleStatementBody().Get());
				Pop();
			}

			void Catch(const CatchClauseNode& clause)
			{
				Push();
				if (clause.HasExceptionName()) Declare(clause.GetExceptionNameToken().GetText());
				Walk(clause.GetBody().Get());
				Walk(clause.GetSingleStatementBody().Get());
				Pop();
			}

			// A nested lambda uses, in this scope, what it captures by name, its init-capture expressions and,
			// with a default capture, whatever its own body uses from outside.
			void Nested(const LambdaNode& nested)
			{
				for (const auto& init : nested.Children()) Walk(init.Get());
				for (const LambdaCapture& c : nested.GetCaptures())
				{
					if (c.kind == LambdaCapture::Kind::Variable) Use(c.name.GetText());
					if (c.kind == LambdaCapture::Kind::This || c.kind == LambdaCapture::Kind::ThisCopy) m_Out.usesThis = true;
				}
				if (nested.GetCaptureDefault() == CaptureDefault::None) return;
				const OuterUses inner = FindOuterUses(nested);
				for (const Core::String& name : inner.names) Use(name);
				m_Out.usesThis = m_Out.usesThis || inner.usesThis;
			}

			void Use(const Core::String& name)
			{
				if (name == kThis) { m_Out.usesThis = true; return; }
				for (auto scope = m_Scopes.rbegin(); scope != m_Scopes.rend(); ++scope)
					if (scope->count(name)) return;
				if (m_Seen.insert(name).second) m_Out.names.Push(name);
			}

			void Declare(const Core::String& name) { if (!m_Scopes.empty()) m_Scopes.back().insert(name); }
			void Push() { m_Scopes.emplace_back(); }
			void Pop() { m_Scopes.pop_back(); }

			OuterUses& m_Out;
			std::vector<std::set<Core::String>> m_Scopes;
			std::set<Core::String> m_Seen;
		};
	}

	OuterUses FindOuterUses(const LambdaNode& lambda)
	{
		OuterUses out;
		OuterUseWalker(out).Lambda(lambda);
		return out;
	}
}

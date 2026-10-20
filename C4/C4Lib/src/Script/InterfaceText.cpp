#include "Script/InterfaceText.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <algorithm>
#include <vector>

namespace Scripting
{
	namespace
	{
		// A range of the text to cut: a body, which leaves `;` behind, or a whole out-of-line definition.
		struct Cut
		{
			size_t start = 0;
			size_t end = 0;
			bool leavesDeclaration = false;
		};

		bool OutOfLine(const FunctionDefinitionNode& fn)
		{
			return fn.GetName() && fn.GetName()->GetSegments().Count() > 1;
		}

		// Where a definition starts: its first annotation, when it has one.
		size_t StartOf(const FunctionDefinitionNode& fn)
		{
			size_t start = fn.startLocation.byteOffset;
			for (const Ref<AnnotationNode>& annotation : fn.GetAnnotations())
				if (annotation) start = std::min(start, annotation->startLocation.byteOffset);
			return start;
		}

		class BodyCutter
		{
		public:
			explicit BodyCutter(const std::unordered_set<const AstNode*>& declaredElsewhere) : m_DeclaredElsewhere(declaredElsewhere) {}

			void Walk(const AstNode* node, bool inTemplate)
			{
				if (!node) return;
				if (const auto* ns = dynamic_cast<const NamespaceNode*>(node))
				{
					for (const Ref<AstNode>& child : ns->Children()) Walk(child.Get(), inTemplate);
				}
				else if (const auto* object = dynamic_cast<const ObjectDefinitionNode*>(node))
				{
					const bool templated = inTemplate || object->IsTemplated() || object->IsSpecialization();
					for (const Ref<AstNode>& member : object->GetBodyMembers()) Walk(member.Get(), templated);
				}
				else if (const auto* un = dynamic_cast<const UnionDefinitionNode*>(node))
				{
					for (const Ref<AstNode>& member : un->GetMembers()) Walk(member.Get(), inTemplate);
				}
				else if (const auto* fn = dynamic_cast<const FunctionDefinitionNode*>(node))
				{
					CutFunction(*fn, inTemplate);
				}
			}

			std::vector<Cut> Take()
			{
				std::sort(m_Cuts.begin(), m_Cuts.end(), [](const Cut& a, const Cut& b) { return a.start < b.start; });
				return std::move(m_Cuts);
			}

		private:
			void CutFunction(const FunctionDefinitionNode& fn, bool inTemplate)
			{
				if (!fn.IsDefinition() || inTemplate || fn.IsTemplated() || fn.GetOwnerTemplateDefinition()) return;
				if (OutOfLine(fn) || m_DeclaredElsewhere.count(&fn)) m_Cuts.push_back({ StartOf(fn), fn.endLocation.byteOffset, false });
				else m_Cuts.push_back({ fn.GetBody()->startLocation.byteOffset, fn.GetBody()->endLocation.byteOffset, true });
			}

			const std::unordered_set<const AstNode*>& m_DeclaredElsewhere;
			std::vector<Cut> m_Cuts;
		};

		// What replaces a cut: `;` for a body, then the cut's line breaks, so later lines keep their numbers.
		std::string Replacement(const std::string& text, const Cut& cut)
		{
			std::string out = cut.leavesDeclaration ? ";" : "";
			out.append(static_cast<size_t>(std::count(text.begin() + cut.start, text.begin() + cut.end, '\n')), '\n');
			return out;
		}
	}

	std::optional<std::string> InterfaceText(const std::string& text, const Core::Vector<Ref<AstNode>>& nodes,
		const std::unordered_set<const AstNode*>& declaredElsewhere)
	{
		BodyCutter cutter(declaredElsewhere);
		for (const Ref<AstNode>& node : nodes) cutter.Walk(node.Get(), false);
		std::string out;
		size_t at = 0;
		for (const Cut& cut : cutter.Take())
		{
			if (cut.start < at || cut.end > text.size() || cut.end < cut.start) return std::nullopt;
			out.append(text, at, cut.start - at);
			out += Replacement(text, cut);
			at = cut.end;
		}
		out.append(text, at, std::string::npos);
		return out;
	}
} // namespace Scripting

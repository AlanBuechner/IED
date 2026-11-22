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

namespace C4Compiler
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

		// Where a location is in the text the parse read (R8: the file's text through its macro layout).
		class ReadText
		{
		public:
			ReadText(std::string_view written, const MacroLayout* macros) : m_Written(written), m_Macros(macros) {}

			size_t At(const SourceLocation& location) const
			{
				if (!m_Macros) return location.byteOffset;
				return m_Macros->RenderedOffset(m_Written, location.byteOffset, location.expansion, location.inExpansion);
			}

		private:
			std::string_view m_Written;
			const MacroLayout* m_Macros;
		};

		bool OutOfLine(const FunctionDefinitionNode& fn)
		{
			return fn.GetName() && fn.GetName()->GetSegments().Count() > 1;
		}

		class BodyCutter
		{
		public:
			BodyCutter(const ReadText& text, const std::unordered_set<const AstNode*>& declaredElsewhere, const std::unordered_set<const AstNode*>& compileTime)
				: m_Text(text), m_DeclaredElsewhere(declaredElsewhere), m_CompileTime(compileTime) {}

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
			// Where a definition starts: its first annotation, when it has one.
			size_t StartOf(const FunctionDefinitionNode& fn) const
			{
				size_t start = m_Text.At(fn.startLocation);
				for (const Ref<AnnotationNode>& annotation : fn.GetAnnotations())
					if (annotation) start = std::min(start, m_Text.At(annotation->startLocation));
				return start;
			}

			void CutFunction(const FunctionDefinitionNode& fn, bool inTemplate)
			{
				if (!fn.IsDefinition() || inTemplate || fn.IsTemplated() || fn.GetOwnerTemplateDefinition() || m_CompileTime.count(&fn)) return;
				if (OutOfLine(fn) || m_DeclaredElsewhere.count(&fn)) m_Cuts.push_back({ StartOf(fn), m_Text.At(fn.endLocation), false });
				else m_Cuts.push_back({ m_Text.At(fn.GetBody()->startLocation), m_Text.At(fn.GetBody()->endLocation), true });
			}

			const ReadText& m_Text;
			const std::unordered_set<const AstNode*>& m_DeclaredElsewhere;
			const std::unordered_set<const AstNode*>& m_CompileTime;
			std::vector<Cut> m_Cuts;
		};

		// What replaces a cut: `;` for a body, then the cut's line breaks, so later lines keep their numbers.
		std::string Replacement(const std::string& text, const Cut& cut)
		{
			std::string out = cut.leavesDeclaration ? ";" : "";
			out.append(static_cast<size_t>(std::count(text.begin() + static_cast<std::ptrdiff_t>(cut.start), text.begin() + static_cast<std::ptrdiff_t>(cut.end), '\n')), '\n');
			return out;
		}
	}

	std::optional<std::string> InterfaceText(std::string_view written, const MacroLayout* macros, const Core::Vector<Ref<AstNode>>& nodes,
		const std::unordered_set<const AstNode*>& declaredElsewhere, const std::unordered_set<const AstNode*>& compileTime)
	{
		const std::string text = macros ? macros->Render(written, 0, static_cast<uint32>(written.size())) : std::string(written);
		const ReadText read(written, macros);
		BodyCutter cutter(read, declaredElsewhere, compileTime);
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
} // namespace C4Compiler

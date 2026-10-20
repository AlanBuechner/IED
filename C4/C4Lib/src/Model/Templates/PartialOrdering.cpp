#include "Model/Templates/PartialOrdering.h"
#include "Parser/Nodes/Declarations/FunPtrTypeNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/PackExpansionNode.h"
#include "Model/Templates/TemplateArguments.h"
#include <string>
#include <algorithm>
#include <unordered_map>

namespace Scripting
{
	namespace
	{
		bool IsParameterOf(const TemplateDefinitionNode* params, const Core::String& name)
		{
			for (uint32 i = 0; params && i < params->ParameterCount(); ++i)
				if (params->GetParameter(i)->GetName() == name) return true;
			return false;
		}

		// The name of a pattern that is one bare name (`T`, `N`), else "".
		Core::String BareName(const AstNode* pattern)
		{
			if (const auto* id = dynamic_cast<const IdentifierExpressionNode*>(pattern)) return id->GetName();
			const auto* q = dynamic_cast<const QualTypeNode*>(pattern);
			if (!q || q->IndirectionCount() || q->IsLeadingConst() || q->FuncPtrType() || !q->BaseName()) return "";
			const auto& segs = q->BaseName()->GetSegments();
			return segs.Count() == 1 && !segs[0].templateArgs ? segs[0].nameToken.GetText() : Core::String();
		}

		// Matches the patterns of `general` (its parameters are variables) against those of `specific` (its
		// parameters are unknown types, rendered `?T` so they never equal a real name).
		class PatternMatch
		{
		public:
			PatternMatch(const TemplatePatterns& general, const TemplatePatterns& specific) : m_General(general), m_Specific(specific) {}

			/// `g`, the pattern of `general`'s trailing expansion, as its element `element` against `s`; each
			/// element binds the pack on its own.
			bool Element(const AstNode* g, const AstNode* s, const Core::String& element)
			{
				m_Element = element;
				const bool matched = Pattern(g, s);
				m_Element.clear();
				return matched;
			}

			bool Pattern(const AstNode* g, const AstNode* s)
			{
				if (!g || !s) return g == s;
				const Core::String variable = BareName(g);
				if (!variable.empty() && IsParameterOf(m_General.params, variable)) return Bind(variable, Render(s));
				const auto* gt = dynamic_cast<const QualTypeNode*>(g);
				const auto* st = dynamic_cast<const QualTypeNode*>(s);
				if (gt && st) return Type(*gt, gt->IndirectionCount(), *st, st->IndirectionCount());
				return Render(g) == Render(s);
			}

		private:
			// A pattern's top-level const (its outermost layer's, or the leading one of a pattern with no layers) is
			// not part of a type (D-30): `T const*` is `T*`, `const T` is `T`.
			bool Type(const QualTypeNode& g, uint32 gLayers, const QualTypeNode& s, uint32 sLayers)
			{
				const bool topLevel = gLayers == g.IndirectionCount() && sLayers == s.IndirectionCount();
				if (gLayers > 0)
				{
					if (sLayers == 0) return false;
					const IndirectionLayer& gl = g.GetIndirections()[gLayers - 1];
					const IndirectionLayer& sl = s.GetIndirections()[sLayers - 1];
					if (gl.indirectionToken != sl.indirectionToken || (!topLevel && gl.isConst != sl.isConst)) return false;
					if ((gl.arraySizeExpr || sl.arraySizeExpr) && !Pattern(gl.arraySizeExpr.Get(), sl.arraySizeExpr.Get())) return false;
					return Type(g, gLayers - 1, s, sLayers - 1);
				}
				const Core::String variable = BaseVariable(g);
				if (!variable.empty())
				{
					if (topLevel) return Bind(variable, Render(s, sLayers, sLayers > 0, true));
					// Below a layer, `const T` takes only a const value, which it strips; a bare `T` takes all that is left.
					if (g.IsLeadingConst()) return s.IsLeadingConst() && sLayers == 0 && Bind(variable, Render(s, 0, false));
					return Bind(variable, Render(s, sLayers, true));
				}
				if (sLayers != 0 || (!topLevel && g.IsLeadingConst() != s.IsLeadingConst())) return false;
				const auto* gf = dynamic_cast<const FunPtrTypeNode*>(g.FuncPtrType().Get());
				const auto* sf = dynamic_cast<const FunPtrTypeNode*>(s.FuncPtrType().Get());
				if (gf || sf) return gf && sf && Signature(*gf, *sf);
				return g.BaseName() && s.BaseName() && Name(*g.BaseName(), *s.BaseName());
			}

			bool Name(const NamespaceQualifiedNameNode& g, const NamespaceQualifiedNameNode& s)
			{
				if (g.SegmentCount() != s.SegmentCount()) return false;
				for (uint32 i = 0; i < g.SegmentCount(); ++i)
				{
					const auto& gs = g.GetSegments()[i];
					const auto& ss = s.GetSegments()[i];
					if (gs.nameToken.GetText() != SegmentName(s, i)) return false;
					const uint32 gCount = gs.templateArgs ? gs.templateArgs->ArgumentCount() : 0;
					const uint32 sCount = ss.templateArgs ? ss.templateArgs->ArgumentCount() : 0;
					if (gCount != sCount) return false;
					for (uint32 a = 0; a < gCount; ++a)
						if (!Pattern(gs.templateArgs->GetArguments()[a].Get(), ss.templateArgs->GetArguments()[a].Get())) return false;
				}
				return true;
			}

			bool Signature(const FunPtrTypeNode& g, const FunPtrTypeNode& s)
			{
				if (g.ParameterCount() != s.ParameterCount() || !g.GetReturnType() != !s.GetReturnType()) return false;
				if (g.GetReturnType() && !Pattern(g.GetReturnType().Get(), s.GetReturnType().Get())) return false;
				for (uint32 i = 0; i < g.ParameterCount(); ++i)
					if (!Pattern(g.GetParameters()[i]->GetType().Get(), s.GetParameters()[i]->GetType().Get())) return false;
				return true;
			}

			bool Bind(const Core::String& variable, const Core::String& rendering)
			{
				const int pack = m_General.params ? PackIndex(*m_General.params) : -1;
				const bool isPack = pack >= 0 && m_General.params->GetParameter(static_cast<uint32>(pack))->GetName() == variable;
				auto [it, added] = m_Bound.emplace(isPack ? variable + m_Element : variable, rendering);
				return added || it->second == rendering;
			}

			Core::String BaseVariable(const QualTypeNode& g) const
			{
				if (g.FuncPtrType() || !g.BaseName()) return "";
				const auto& segs = g.BaseName()->GetSegments();
				if (segs.Count() != 1 || segs[0].templateArgs) return "";
				const Core::String& name = segs[0].nameToken.GetText();
				return IsParameterOf(m_General.params, name) ? name : Core::String();
			}

			Core::String SegmentName(const NamespaceQualifiedNameNode& name, uint32 i) const
			{
				const Core::String& text = name.GetSegments()[i].nameToken.GetText();
				return name.SegmentCount() == 1 && IsParameterOf(m_Specific.params, text) ? "?" + text : text;
			}

			Core::String Render(const AstNode* pattern) const
			{
				if (const auto* q = dynamic_cast<const QualTypeNode*>(pattern)) return Render(*q, q->IndirectionCount(), q->IndirectionCount() > 0, true);
				const Core::String name = BareName(pattern);
				if (!name.empty() && IsParameterOf(m_Specific.params, name)) return "?" + name;
				return pattern ? pattern->ToString() : Core::String();
			}

			/// `s` up to its layer `layers`; `topLevel` leaves out the outermost layer's const when all are rendered.
			Core::String Render(const QualTypeNode& s, uint32 layers, bool withLeadingConst, bool topLevel = false) const
			{
				Core::String out = withLeadingConst && s.IsLeadingConst() ? "const " : "";
				if (s.FuncPtrType()) out += s.FuncPtrType()->ToString();
				else if (s.BaseName())
					for (uint32 i = 0; i < s.BaseName()->SegmentCount(); ++i)
					{
						const auto& seg = s.BaseName()->GetSegments()[i];
						out += (i ? "::" : "") + SegmentName(*s.BaseName(), i);
						if (!seg.templateArgs) continue;
						out += "<";
						for (uint32 a = 0; a < seg.templateArgs->ArgumentCount(); ++a)
							out += (a ? "," : "") + Render(seg.templateArgs->GetArguments()[a].Get());
						out += ">";
					}
				for (uint32 i = 0; i < layers; ++i)
				{
					const IndirectionLayer& layer = s.GetIndirections()[i];
					const bool outermost = topLevel && i + 1 == layers && layers == s.IndirectionCount();
					out += layer.isConst && !outermost ? " const" : "";
					if (layer.indirectionToken == TokenType::Star) out += "*";
					else if (layer.indirectionToken == TokenType::Ampersand) out += "&";
					else out += "[" + Render(layer.arraySizeExpr.Get()) + "]";
				}
				return out;
			}

			const TemplatePatterns& m_General;
			const TemplatePatterns& m_Specific;
			std::unordered_map<Core::String, Core::String> m_Bound;
			Core::String m_Element; // `#i` while an expansion's element i is matched
		};
	}

	TemplatePatterns TemplatePatterns::OfArguments(const TemplateArgumentListNode* arguments, const TemplateDefinitionNode* params)
	{
		TemplatePatterns p;
		p.params = params;
		const uint32 count = arguments ? arguments->ArgumentCount() : 0;
		for (uint32 i = 0; i < count; ++i)
		{
			const AstNode* argument = arguments->GetArguments()[i].Get();
			const auto* expansion = dynamic_cast<const PackExpansionNode*>(argument);
			if (expansion && i + 1 == count) p.expansion = expansion->GetPattern().Get();
			else p.patterns.Push(argument);
		}
		return p;
	}

	bool PartialOrdering::AtLeastAsSpecialized(const TemplatePatterns& specific, const TemplatePatterns& general)
	{
		const uint32 fixed = general.patterns.Count();
		if (specific.patterns.Count() < fixed) return false;
		if (!general.expansion && (specific.patterns.Count() != fixed || specific.expansion)) return false;
		PatternMatch match(general, specific);
		for (uint32 i = 0; i < fixed; ++i)
			if (!match.Pattern(general.patterns[i], specific.patterns[i])) return false;
		for (uint32 i = fixed; i < specific.patterns.Count(); ++i)
			if (!match.Element(general.expansion, specific.patterns[i], "#" + std::to_string(i - fixed))) return false;
		return !specific.expansion || match.Element(general.expansion, specific.expansion, "#...");
	}

	int PartialOrdering::Compare(const TemplatePatterns& a, const TemplatePatterns& b)
	{
		const bool aAsSpecialized = AtLeastAsSpecialized(a, b);
		const bool bAsSpecialized = AtLeastAsSpecialized(b, a);
		if (aAsSpecialized && !bAsSpecialized) return 1;
		if (bAsSpecialized && !aAsSpecialized) return -1;
		return 0;
	}
} // namespace Scripting

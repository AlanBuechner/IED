#include "Compiler/DeclarationFingerprint.h"
#include "Lexer/Lexer.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <vector>

namespace C4Compiler
{
	namespace
	{
		constexpr uint64 c_FnvOffset = 14695981039346656037ull;
		constexpr uint64 c_FnvPrime = 1099511628211ull;
		constexpr const char* c_CompileTime = "compile_time";

		// The bytes [begin, end) of the file a left-out body spans.
		struct BodySpan
		{
			uint32 begin = 0;
			uint32 end = 0;
		};

		void Mix(uint64& hash, const void* data, size_t size)
		{
			const auto* bytes = static_cast<const uint8*>(data);
			for (size_t i = 0; i < size; ++i)
			{
				hash ^= bytes[i];
				hash *= c_FnvPrime;
			}
		}

		bool IsCompileTime(const FunctionDefinitionNode& fn)
		{
			for (const auto& annotation : fn.GetAnnotations())
				if (annotation)
					for (const auto& group : annotation->GetBracketGroups())
						for (const auto& item : group.annotations)
							if (item && item->GetName() == c_CompileTime) return true;
			return false;
		}

		void CollectBodies(const AstNode& node, uint32 sourceId, std::vector<BodySpan>& bodies)
		{
			if (const auto* fn = dynamic_cast<const FunctionDefinitionNode*>(&node))
			{
				const Ref<CodeBlockNode>& body = fn->GetBody();
				if (BodyIsItsOwn(*fn) && body && body->startLocation.sourceId == sourceId)
					bodies.push_back({ body->startLocation.byteOffset, body->endLocation.byteOffset });
				return;
			}
			if (const auto* object = dynamic_cast<const ObjectDefinitionNode*>(&node); object && object->GetTemplateDefinition())
				return; // a class template's methods are instantiated with it
			for (const Ref<AstNode>& child : node.Children())
				if (child) CollectBodies(*child, sourceId, bodies);
		}

		bool InBody(const std::vector<BodySpan>& bodies, uint32 byteOffset)
		{
			for (const BodySpan& body : bodies)
				if (byteOffset >= body.begin && byteOffset < body.end) return true;
			return false;
		}
	}

	bool BodyIsItsOwn(const FunctionDefinitionNode& fn)
	{
		return fn.IsDefinition() && !fn.IsInline() && !fn.IsTemplated() && !fn.GetOwnerTemplateDefinition() && !IsCompileTime(fn);
	}

	uint64 DeclarationFingerprint(const Source& source, uint32 sourceId, const MacroLayout* macros,
		const Core::Vector<Ref<AstNode>>& children)
	{
		std::vector<BodySpan> bodies;
		for (const Ref<AstNode>& child : children)
			if (child) CollectBodies(*child, sourceId, bodies);

		uint64 hash = c_FnvOffset;
		Lexer lexer(source, sourceId, macros);
		for (const Token* token = &lexer.NextToken(); token->GetType() != TokenType::EndOfFile; token = &lexer.NextToken())
		{
			const TokenType type = token->GetType();
			if (type == TokenType::Comment || type == TokenType::MultiComment) continue;
			if (InBody(bodies, token->GetStartLocation().byteOffset)) continue;
			Mix(hash, &type, sizeof(type));
			const Core::String& text = token->GetText();
			Mix(hash, text.data(), text.size());
			Mix(hash, "", 1); // between tokens: `ab` `c` is not `a` `bc`
		}
		return hash;
	}
} // namespace C4Compiler

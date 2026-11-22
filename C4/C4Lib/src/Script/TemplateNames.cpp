#include "Script/TemplateNames.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include <algorithm>

namespace C4Compiler
{
	namespace
	{
		constexpr char kAdditionSeparator = '|';

		void Add(std::vector<std::string>& names, const std::string& name)
		{
			if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
		}

		std::string Joined(const std::vector<std::string>& parts)
		{
			std::string joined;
			for (const std::string& part : parts) joined += (joined.empty() ? "" : "::") + part;
			return joined;
		}

		// The first `count` segments of `name`, joined: `Ns::Box` of `Ns::Box::Set` for count 2.
		std::string Segments(const NamespaceQualifiedNameNode& name, uint32 count)
		{
			std::vector<std::string> parts;
			for (uint32 i = 0; i < count && i < name.GetSegments().Count(); ++i) parts.push_back(name.GetSegments()[i].nameToken.GetText().c_str());
			return Joined(parts);
		}

		class Walker
		{
		public:
			void Walk(const AstNode* node)
			{
				if (!node) return;
				if (const auto* ns = dynamic_cast<const NamespaceNode*>(node)) Namespace(*ns);
				else if (const auto* object = dynamic_cast<const ObjectDefinitionNode*>(node)) Object(*object);
				else if (const auto* un = dynamic_cast<const UnionDefinitionNode*>(node))
					for (const Ref<AstNode>& member : un->GetMembers()) Walk(member.Get());
				else if (const auto* fn = dynamic_cast<const FunctionDefinitionNode*>(node)) Function(*fn);
				else if (const auto* alias = dynamic_cast<const AliasDefinitionNode*>(node))
				{
					if (alias->IsTemplated()) Declare(alias->GetNameToken().GetText().c_str());
				}
			}

			TemplateNames Take() { return std::move(m_Names); }

		private:
			void Namespace(const NamespaceNode& ns)
			{
				const bool named = !ns.IsRoot() && !ns.Name().empty();
				if (named) m_Namespaces.push_back(ns.Name().c_str());
				for (const Ref<AstNode>& child : ns.Children()) Walk(child.Get());
				if (named) m_Namespaces.pop_back();
			}

			void Object(const ObjectDefinitionNode& object)
			{
				const std::string name = object.GetNameToken().GetText().c_str();
				if (object.IsSpecialization()) Extend(name);
				else if (object.IsTemplated() || HoldsMemberTemplate(object)) Declare(name);
				m_Classes.push_back(name);
				for (const Ref<AstNode>& member : object.GetBodyMembers()) Walk(member.Get());
				m_Classes.pop_back();
			}

			static bool HoldsMemberTemplate(const ObjectDefinitionNode& object)
			{
				for (const Ref<AstNode>& member : object.GetBodyMembers())
					if (const auto* fn = dynamic_cast<const FunctionDefinitionNode*>(member.Get()); fn && fn->IsTemplated()) return true;
				return false;
			}

			// A templated function declares a template; written out of line it adds to its owner's, a specialization to
			// the one it specializes, an extension template to its receiver's.
			void Function(const FunctionDefinitionNode& fn)
			{
				if (!fn.GetName() || fn.GetName()->GetSegments().Empty()) return;
				const bool templated = fn.IsTemplated() || fn.GetOwnerTemplateDefinition();
				if (!templated) return;
				const uint32 segments = fn.GetName()->GetSegments().Count();
				if (segments > 1) Extend(Segments(*fn.GetName(), segments - 1));
				else if (fn.IsTemplated() && fn.GetTemplateDefinition()->ParameterCount() == 0) Extend(Segments(*fn.GetName(), segments));
				else Declare(Segments(*fn.GetName(), segments));
				if (fn.IsExtensionMethod() && fn.GetExtendsTypeName()) Extend(Segments(*fn.GetExtendsTypeName(), fn.GetExtendsTypeName()->GetSegments().Count()));
			}

			void Declare(const std::string& name)
			{
				std::vector<std::string> path = m_Namespaces;
				path.insert(path.end(), m_Classes.begin(), m_Classes.end());
				path.push_back(name);
				Add(m_Names.declared, Joined(path));
			}

			void Extend(const std::string& written) { Add(m_Names.extended, Joined(m_Namespaces) + kAdditionSeparator + written); }

			std::vector<std::string> m_Namespaces;
			std::vector<std::string> m_Classes;
			TemplateNames m_Names;
		};
	}

	TemplateNames TemplateNamesOf(const Core::Vector<Ref<AstNode>>& nodes)
	{
		Walker walker;
		for (const Ref<AstNode>& node : nodes) walker.Walk(node.Get());
		return walker.Take();
	}

	std::vector<std::string> TemplateAdditionTargets(const std::string& extended)
	{
		std::vector<std::string> targets;
		const size_t separator = extended.find(kAdditionSeparator);
		if (separator == std::string::npos) return targets;
		std::string scope = extended.substr(0, separator);
		const std::string written = extended.substr(separator + 1);
		for (;;)
		{
			targets.push_back(scope.empty() ? written : scope + "::" + written);
			if (scope.empty()) return targets;
			const size_t outer = scope.rfind("::");
			scope = outer == std::string::npos ? std::string() : scope.substr(0, outer);
		}
	}
} // namespace C4Compiler

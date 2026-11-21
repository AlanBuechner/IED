#include "Backend/Reflection/ReflectsCheck.h"
#include "Backend/Lowering/LoweredProgram.h"
#include "Backend/Reflection/ReflectionScope.h"
#include "Compiler/ErrorTracker.h"
#include "Model/Annotations.h"
#include "Model/ProgramModel.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/QualifiedName.h"
#include "Model/Types/UnionType.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include <set>
#include <string>
#include <unordered_set>
#include <utility>

namespace Scripting
{
	namespace
	{
		constexpr const char* kReflects = "reflects";

		// The `[reflects("T")]` the function's declarations carry: the parameter's name and where it is written.
		struct Reflected
		{
			Core::String parameter;
			SourceLocation where;
		};

		std::vector<Reflected> ReflectsOf(const FunctionRecord& function)
		{
			std::vector<Reflected> found;
			for (const AstNode* declaration : function.declarations)
				if (const Core::Vector<Ref<AnnotationNode>>* annotations = DeclarationAnnotations(declaration))
					for (const Ref<AnnotationNode>& node : *annotations)
						for (const auto& group : node->GetBracketGroups())
							for (const auto& item : group.annotations)
							{
								if (!item || item->GetName() != kReflects) continue;
								const auto* literal = item->GetArguments().Count() == 1 ? dynamic_cast<const LiteralNode*>(item->GetArguments()[0].Get()) : nullptr;
								found.push_back({ literal ? Core::String(literal->GetValue()) : Core::String(), item->startLocation });
							}
			return found;
		}

		const Type* Bound(const InstantiationBindings& bindings, const Core::String& parameter)
		{
			for (const auto& [name, type] : bindings.typeParams)
				if (name == parameter) return type;
			return nullptr;
		}

		const AstNode* DeclarationOf(const Type& type)
		{
			if (const auto* object = dynamic_cast<const ObjectType*>(&type)) return object->GetDeclNode();
			if (const auto* en = dynamic_cast<const EnumType*>(&type)) return en->GetDeclNode();
			if (const auto* un = dynamic_cast<const UnionType*>(&type)) return un->GetDeclNode();
			return nullptr;
		}

		// The type as a serializer looks it up: through aliases, arrays and pointers to what they hold.
		const Type* Named(const ProgramModel& model, const Type* type)
		{
			while (type)
			{
				if (type->GetKind() == TypeKind::Alias) type = static_cast<const AliasType*>(type)->GetAliasedType();
				else if (const Type* element = model.ElementOf(type)) type = element;
				else return type;
			}
			return nullptr;
		}

		// Walks what reflection reads of a value of a type: its fields and its template arguments, each type once.
		class Walk
		{
		public:
			Walk(const ProgramModel& model, const ReflectionScope& scope) : m_Model(model), m_Scope(scope) {}

			// The first type out of scope `type` holds and the path to it ("Save.inventory"), or null.
			const Type* FirstOut(const Type* type, const std::string& path, std::string& at)
			{
				type = Named(m_Model, type);
				if (!type || !m_Seen.insert(type).second) return nullptr;
				const TypeKind kind = type->GetKind();
				if (kind != TypeKind::Object && kind != TypeKind::Enum && kind != TypeKind::Union) return nullptr;
				if (!m_Scope.Has(*type))
				{
					at = path;
					return type;
				}
				const auto* object = dynamic_cast<const ObjectType*>(type);
				if (!object) return nullptr;
				for (const ObjectType::TemplateArgument& argument : object->GetTemplateArguments())
					if (const Type* out = FirstOut(argument.type, path, at)) return out;
				for (const auto& field : object->GetFields())
					if (!field.m_IsStatic)
						if (const Type* out = FirstOut(field.m_Type, path + "." + field.mName.c_str(), at)) return out;
				return nullptr;
			}

		private:
			const ProgramModel& m_Model;
			const ReflectionScope& m_Scope;
			std::unordered_set<const Type*> m_Seen;
		};
	}

	void CheckReflectedArguments(const ProgramModel& model, const LoweredProgram& program, const ReflectionScope& scope, ErrorTracker& errors)
	{
		std::set<std::pair<const Type*, std::string>> reported;
		std::set<std::tuple<uint32, uint32>> misused;
		for (const LoweredUnit& unit : program.units)
		{
			if (unit.kind != UnitKind::Function || !unit.tree || !unit.tree->record) continue;
			const FunctionRecord& function = *unit.tree->record;
			const std::vector<Reflected> reflects = ReflectsOf(function);
			if (reflects.empty()) continue;
			const InstantiationBindings* bindings = model.FunctionInstantiationBindings(&function);
			for (const Reflected& reflected : reflects)
			{
				const Type* argument = bindings ? Bound(*bindings, reflected.parameter) : nullptr;
				if (!argument)
				{
					if (misused.insert({ reflected.where.sourceId, reflected.where.byteOffset }).second)
						errors.AddError(reflected.where, Core::String(("[reflects] names a type parameter of its function template (\"" +
							std::string(reflected.parameter.c_str()) + "\" is none): [reflects(\"T\")] says the function reads its T through reflection (D-89)").c_str()));
					continue;
				}
				std::string at;
				const std::string root = QualifiedName(Named(model, argument)).c_str();
				const Type* out = Walk(model, scope).FirstOut(argument, root, at);
				if (!out) continue;
				const std::string instance = std::string(function.name.c_str()) + "<" + QualifiedName(argument).c_str() + ">";
				if (!reported.insert({ out, instance }).second) continue;
				const AstNode* declaration = DeclarationOf(*out);
				const std::string through = at == root ? "" : " (" + at + ")";
				errors.AddError(declaration ? declaration->startLocation : reflected.where, Core::String(("'" + std::string(QualifiedName(out).c_str()) +
					"' is out of reflection's scope (" + scope.WhyOut(*out) + "), but " + instance + " reads it through reflection" + through +
					": bring it back with [reflect(\"on\")], or do not pass it there (D-89)").c_str()));
			}
		}
	}
}

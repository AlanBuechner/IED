#include "Backend/Reflection/RegistrationEmitter.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Backend/HIR/InvokeThunk.h"
#include "Model/Annotations.h"
#include "Model/ProgramModel.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/QualifiedName.h"
#include "Model/Types/SwizzleType.h"
#include "Model/Types/UnionLayout.h"
#include "Model/Types/UnionType.h"
#include "Model/Types/VirtualTables.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "ReflectABI.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/xxhash.h>
#include <set>
#include <tuple>
#include <string_view>
#include <unordered_map>

namespace Scripting
{
	namespace
	{
		std::string Scoped(const Core::Vector<Core::String>& enclosing, const Core::String& name)
		{
			std::string out;
			for (const Core::String& part : enclosing) out += std::string(part.c_str()) + "::";
			return out + name.c_str();
		}

		// The names of the program's symbols are long: hashed with xxh3 rather than a byte at a time.
		struct NameHash
		{
			size_t operator()(std::string_view name) const { return llvm::xxh3_64bits(llvm::StringRef(name.data(), name.size())); }
		};
		using Names = std::unordered_set<std::string_view, NameHash>;

		bool Reflected(const ProgramModel& model, const ObjectType* object)
		{
			return object && object->IsDefined() && !model.ClosureOf(object) && !object->IsReturnList() && !object->GetName().empty();
		}

		// A field a serializer reads and writes: one outside any union, or one of its union's first view. The others
		// name the same bytes again (BUG-194).
		bool IsAlias(const Core::Vector<ObjectType::FieldInfo>& fields, const ObjectType::FieldInfo& field, bool inUnion)
		{
			return inUnion && !UnionLayout::IsPrimary(UnionLayout::RegionMembers(fields, field.m_UnionRegionId), field);
		}

		// The rows of the image, described entity by entity (ImageDescription); each table a row points at becomes a
		// private constant where the image is built.
		class ImageWriter
		{
		public:
			ImageWriter(ModuleContext& module, const ProgramModel& model, const InvokeThunks& thunks, ErrorTracker& errors)
				: m_Module(module), m_Model(model), m_Thunks(thunks), m_Errors(errors) {}

			void AddType(const ObjectType& object)
			{
				const auto& bases = object.GetBaseTypes();
				const Table fields = Fields(object);
				const Table methods = Methods(object);
				const Table groups = Groups(object.GetGroups());
				const Table annotations = Annotations(object.GetDeclNode(), m_Model.TypeScope(&object));
				m_Types.push_back({ U32(reflect_KIND_OBJECT), TypeText(&object), U64(object.GetSize()),
					U64(object.GetAlignment()), fields.count, fields.rows, methods.count, methods.rows,
					Text(!bases.Empty() && bases[0] ? QualifiedName(bases[0]) : std::string()), U32(0), Null(),
					groups.count, groups.rows, annotations.count, annotations.rows });
			}

			void AddUnion(const UnionType& un)
			{
				std::vector<ImageRow> rows;
				for (const auto& field : un.GetFields())
					if (!field.m_IsStatic && field.m_Type)
						rows.push_back(FieldRow(field, field.m_Offset, IsAlias(un.GetFields(), field, true), un.GetDeclScope()));
				const Table fields = Rows(ImageShape::Field, std::move(rows));
				const Table groups = Groups(un.GetGroups());
				const Table annotations = Annotations(un.GetDeclNode(), un.GetDeclScope());
				m_Types.push_back({ U32(reflect_KIND_UNION), TypeText(&un), U64(un.GetSize()),
					U64(un.GetAlignment()), fields.count, fields.rows, U32(0), Null(), Text(""), U32(0), Null(),
					groups.count, groups.rows, annotations.count, annotations.rows });
			}

			void AddEnum(const EnumType& en)
			{
				std::vector<ImageRow> values;
				for (const auto& member : en.GetMembers())
					values.push_back({ Text(member.mName), I64(member.m_Value) });
				const Table annotations = Annotations(en.GetDeclNode(), en.GetDeclScope());
				m_Types.push_back({ U32(reflect_KIND_ENUM), TypeText(&en), U64(en.GetSize()),
					U64(en.GetAlignment()), U32(0), Null(), U32(0), Null(), Text(""), U32(en.GetMemberCount()),
					m_Image.Table(ImageShape::EnumValue, std::move(values)), U32(0), Null(), annotations.count, annotations.rows });
			}

			void AddVariable(const SymbolInfo& variable)
			{
				const Scope* scope = variable.declaringScope;
				const ObjectType* owner = scope && scope->GetKind() == Scope::Kind::Class ? scope->objectType : nullptr;
				const Core::String plain = variable.plainName.empty() ? variable.mangledName : variable.plainName;
				const Table groups = Groups(variable.groups);
				const Table annotations = Annotations(variable.declNode, scope);
				m_Variables.push_back({ Text(Scoped(EnclosingNames(scope), plain)), Text(variable.mangledName),
					TypeText(variable.type), U64(variable.type->GetSize()), U64(variable.type->GetAlignment()), U32(variable.isConst ? 1 : 0),
					Text(owner ? QualifiedName(owner) : std::string()), groups.count, groups.rows, annotations.count,
					annotations.rows });
				m_Addresses.push_back({ Symbol(m_Module.GlobalAddress(variable)) });
			}

			void AddFunction(const FunctionRecord& function)
			{
				const ObjectType* owner = function.containingType;
				const Core::String plain = function.name.empty() ? function.mangledName : function.name;
				const std::string name = owner ? std::string(plain.c_str()) : Scoped(EnclosingNames(function.scope), plain);
				Core::Vector<const Type*> types;
				for (const auto& parameter : function.type->GetParameters()) types.Push(parameter.m_Type);
				const Table params = Params(&function, types);
				const Table groups = Groups(function.groups);
				const Table annotations = RecordAnnotations(&function);
				const InvokeEntry* entry = ThunkEntry(&function);
				m_Functions.push_back({ Text(name), Text(function.mangledName),
					TypeText(function.type->GetReturnType()), params.count, params.rows, U32(function.isStatic || !owner ? 1 : 0),
					U32(function.type->IsVariadic() ? 1 : 0), owner ? TypeText(owner) : Text(std::string()),
					groups.count, groups.rows, Invoke(entry), annotations.count, annotations.rows, Code(entry), Address(entry) });
			}

			ImageDescription Finish()
			{
				if (m_Types.empty() && m_Variables.empty() && m_Functions.empty()) return std::move(m_Image);
				const uint64 types = m_Types.size();
				const uint64 variables = m_Variables.size();
				const uint64 functions = m_Functions.size();
				const ImageValue typeRows = m_Image.Table(ImageShape::TypeInfo, std::move(m_Types));
				const ImageValue variableRows = m_Image.Table(ImageShape::Variable, std::move(m_Variables));
				const ImageValue addresses = m_Image.Table(ImageShape::Pointer, std::move(m_Addresses));
				const ImageValue functionRows = m_Image.Table(ImageShape::Function, std::move(m_Functions));
				m_Image.SetImage({ U32(types), typeRows, U32(variables), variableRows, addresses, U32(functions), functionRows });
				return std::move(m_Image);
			}

		private:
			// A table field pair: how many rows, and where they are (null for none).
			struct Table
			{
				ImageValue count;
				ImageValue rows;
			};

			ImageValue U32(uint64 value) const { return m_Image.Integer(value); }
			ImageValue U64(uint64 value) const { return m_Image.Integer(value); }
			ImageValue I64(int64 value) const { return m_Image.Integer(static_cast<uint64>(value)); }
			ImageValue F64(double value) const { return m_Image.Real(value); }
			ImageValue Null() const { return m_Image.Null(); }
			ImageValue Text(std::string text) { return m_Image.Text(std::move(text)); }
			// A type's name is one text however many rows name it.
			ImageValue TypeText(const Type* type)
			{
				auto [known, fresh] = m_TypeTexts.try_emplace(type);
				if (fresh) known->second = Text(QualifiedName(type));
				return known->second;
			}
			// A function's or global's name is one symbol however many rows name it (shared thunks, a method's code).
			ImageValue Symbol(const llvm::GlobalValue* value)
			{
				if (!value) return Null();
				auto [known, fresh] = m_SymbolOf.try_emplace(value);
				if (fresh) known->second = m_Image.Symbol(value->getName().str(), llvm::isa<llvm::Function>(value), value->hasHiddenVisibility());
				return known->second;
			}

			Table Rows(ImageShape shape, std::vector<ImageRow> rows)
			{
				const uint64 count = rows.size();
				return { U32(count), m_Image.Table(shape, std::move(rows)) };
			}

			// The function's thunk and the code it calls; null when reflection cannot call it.
			const InvokeEntry* ThunkEntry(const FunctionRecord* function) const
			{
				auto found = function ? m_Thunks.find(function) : m_Thunks.end();
				return found == m_Thunks.end() ? nullptr : &found->second;
			}

			// The function's invoke thunk, or null when reflection cannot call it.
			ImageValue Invoke(const InvokeEntry* entry) { return Symbol(entry ? entry->thunk : nullptr); }

			// The code the function's shared thunk calls, or null for a thunk of its own.
			ImageValue Code(const InvokeEntry* entry) { return Symbol(entry ? entry->code : nullptr); }

			// The function's own code, which FunctionContaining finds a return address in; null when it has no thunk.
			ImageValue Address(const InvokeEntry* entry) { return Symbol(entry ? entry->function : nullptr); }

			Table Groups(const Core::Vector<Core::String>& groups)
			{
				std::vector<ImageRow> names;
				for (const Core::String& group : groups) names.push_back({ Text(group) });
				return Rows(ImageShape::Pointer, std::move(names));
			}

			ImageRow ArgumentRow(const ConstantValue& value)
			{
				switch (value.kind)
				{
				case ConstantValue::Kind::Bool:
					return { U32(reflect_ARG_BOOL), I64(value.booleanValue ? 1 : 0), F64(0), Text("") };
				case ConstantValue::Kind::Float64:
					return { U32(reflect_ARG_FLOAT), I64(0), F64(value.floatValue), Text("") };
				case ConstantValue::Kind::String:
					return { U32(reflect_ARG_STRING), I64(0), F64(0), Text(value.stringValue) };
				default:
					return { U32(reflect_ARG_INT), I64(value.intValue), F64(0), Text("") };
				}
			}

			// The reflected annotations of `declarations` folded in `scope`. An argument that does not fold is reported
			// once, and the entity keeps no annotations.
			Table Annotations(const Core::Vector<const AstNode*>& declarations, const Scope* scope)
			{
				auto folded = ReflectedAnnotations(m_Model, declarations, scope);
				if (!folded.Ok())
				{
					if (m_Reported.insert({ folded.loc.sourceId, folded.loc.line, folded.loc.column }).second) m_Errors.AddError(folded.loc, folded.message);
					return { U32(0), Null() };
				}
				std::vector<ImageRow> rows;
				for (const ReflectedAnnotation& annotation : folded.value)
				{
					std::vector<ImageRow> arguments;
					for (const ConstantValue& argument : annotation.arguments) arguments.push_back(ArgumentRow(argument));
					const Table table = Rows(ImageShape::AnnotationArg, std::move(arguments));
					rows.push_back({ Text(annotation.name), table.count, table.rows });
				}
				return Rows(ImageShape::Annotation, std::move(rows));
			}

			Table Annotations(const AstNode* declaration, const Scope* scope)
			{
				Core::Vector<const AstNode*> declarations;
				if (declaration) declarations.Push(declaration);
				return Annotations(declarations, scope);
			}

			// A function's annotations, made once for its method entry and its function entry.
			Table RecordAnnotations(const FunctionRecord* function)
			{
				if (!function) return { U32(0), Null() };
				auto known = m_RecordAnnotations.find(function);
				if (known != m_RecordAnnotations.end()) return known->second;
				const Table table = Annotations(function->declarations, function->scope);
				m_RecordAnnotations.emplace(function, table);
				return table;
			}

			// Parameter `index`'s annotations, from each declaration whose parameter list it can be found in.
			Table ParamAnnotations(const FunctionRecord* function, uint32 index)
			{
				if (!function) return { U32(0), Null() };
				Core::Vector<const AstNode*> parameters;
				for (const AstNode* declaration : function->declarations)
				{
					const auto* written = dynamic_cast<const FunctionDefinitionNode*>(declaration);
					if (written && written->GetParameters().Count() == function->type->GetParameterCount())
						parameters.Push(written->GetParameters()[index].Get());
				}
				return Annotations(parameters, function->scope);
			}

			Table Params(const FunctionRecord* function, const Core::Vector<const Type*>& types)
			{
				if (function)
					if (auto known = m_RecordParams.find(function); known != m_RecordParams.end()) return known->second;
				std::vector<ImageRow> rows;
				for (uint32 i = 0; i < types.Count(); ++i)
				{
					const Core::String name = function && i < function->paramNames.Count() ? function->paramNames[i] : Core::String();
					const Table annotations = ParamAnnotations(function, i);
					rows.push_back({ Text(name), TypeText(types[i]), annotations.count, annotations.rows });
				}
				const Table table = Rows(ImageShape::Param, std::move(rows));
				if (function) m_RecordParams.emplace(function, table);
				return table;
			}

			ImageRow FieldRow(const ObjectType::FieldInfo& field, uint64 offset, bool alias, const Scope* scope)
			{
				const Table annotations = Annotations(field.m_DeclaringNode, scope);
				return { Text(field.mName), TypeText(field.m_Type), U64(field.m_Type->GetSize()), U64(offset),
					U32(field.m_IsConst ? 1 : 0), U32(alias ? 1 : 0), annotations.count, annotations.rows };
			}

			ImageRow LaneRow(const std::string& name, const std::string& typeName, uint64 size, uint64 offset, bool isConst, bool alias)
			{
				return { Text(name), Text(typeName), U64(size), U64(offset), U32(isConst ? 1 : 0), U32(alias ? 1 : 0), U32(0), Null() };
			}

			// A packed region is reflected through its lanes, each one element wide (BUG-193): the first name of each
			// lane is its field, a later view of the same lane (`r` beside `x`) and the whole-vector array (`data`)
			// are aliases.
			void AddLaneRows(const ObjectType::FieldInfo& storage, std::vector<ImageRow>& rows)
			{
				const auto* region = static_cast<const SwizzleType*>(storage.m_Type);
				const Type* element = region->GetElementType();
				const uint64 width = element ? element->GetSize() : 0;
				const std::string elementName = QualifiedName(element).c_str();
				std::set<uint32> named;
				for (const SwizzleType::LaneInfo& lane : region->GetLanes())
				{
					const bool alias = !named.insert(lane.m_Index).second;
					rows.push_back(LaneRow(lane.m_Name.c_str(), elementName, width, storage.m_Offset + lane.m_Index * width, storage.m_IsConst, alias));
				}
				if (region->GetArrayAlias().empty()) return;
				const uint32 lanes = region->GetLaneCount();
				rows.push_back(LaneRow(region->GetArrayAlias().c_str(), elementName + "[" + std::to_string(lanes) + "]", width * lanes,
					storage.m_Offset, storage.m_IsConst, !named.empty()));
			}

			Table Fields(const ObjectType& object)
			{
				std::vector<ImageRow> rows;
				for (const auto& field : object.GetFields())
				{
					if (field.m_IsStatic || !field.m_Type) continue; // a static has its own storage, registered as a variable
					if (field.m_IsPackedStorage) { AddLaneRows(field, rows); continue; }
					rows.push_back(FieldRow(field, field.m_Offset, IsAlias(object.GetFields(), field, field.m_InUnionRegion), m_Model.TypeScope(&object)));
				}
				return Rows(ImageShape::Field, std::move(rows));
			}

			Table Methods(const ObjectType& object)
			{
				std::vector<ImageRow> rows;
				for (const auto& method : object.GetMethods())
				{
					const FunctionRecord* record = m_Model.RecordForMethod(&object, &method);
					const Table params = Params(record, method.m_ParameterTypes);
					const Table annotations = RecordAnnotations(record);
					const InvokeEntry* entry = ThunkEntry(record);
					rows.push_back({ Text(method.mName), params.count, params.rows, TypeText(method.m_ReturnType),
						U32(method.m_IsStatic ? 1 : 0), U32(VirtualTables::IsVirtual(&object, method) ? 1 : 0), U32(method.m_IsConst ? 1 : 0),
						Invoke(entry), annotations.count, annotations.rows, Code(entry) });
				}
				return Rows(ImageShape::Method, std::move(rows));
			}

			ModuleContext& m_Module;
			ImageDescription m_Image;
			const ProgramModel& m_Model;
			const InvokeThunks& m_Thunks;
			ErrorTracker& m_Errors;
			std::set<std::tuple<uint32, uint32, uint32>> m_Reported; // annotation failures already reported, by location
			std::unordered_map<const FunctionRecord*, Table> m_RecordAnnotations;
			std::unordered_map<const FunctionRecord*, Table> m_RecordParams;
			std::unordered_map<const Type*, ImageValue> m_TypeTexts;
			std::unordered_map<const llvm::GlobalValue*, ImageValue> m_SymbolOf;
			std::vector<ImageRow> m_Types;
			std::vector<ImageRow> m_Variables;
			std::vector<ImageRow> m_Addresses;
			std::vector<ImageRow> m_Functions;
		};
	}

	bool ReflectedFunction(const FunctionRecord& function)
	{
		return function.type && !function.isLocal && !function.isBuiltin && function.synthesized == LifecycleOp::None &&
			!function.mangledName.empty() && function.mangledName.rfind("c4_", 0) != 0;
	}

	std::string InvokeThunkSymbol(const FunctionRecord& function) { return "__c4_invoke_" + EscapedText(function.mangledName); }

	std::string SharedInvokeThunkSymbol(const FunctionRecord& function) { return "__c4_invoke_sig_" + EscapedText(InvokeSignature(function)); }

	std::vector<std::string> ReflectedTypeNames(const ProgramModel& model)
	{
		std::vector<std::string> names;
		for (const ObjectType* object : model.ObjectTypes())
			if (Reflected(model, object)) names.push_back(QualifiedName(object).c_str());
		for (const EnumType* en : model.EnumTypes())
			if (en && !en->GetName().empty()) names.push_back(QualifiedName(en).c_str());
		for (const UnionType* un : model.UnionTypes())
			if (un && !un->GetName().empty()) names.push_back(QualifiedName(un).c_str());
		return names;
	}

	ImageDescription DescribeReflectionImage(ModuleContext& module, const ProgramModel& model, const InvokeThunks& thunks,
		const HostEntities& host, ErrorTracker& errors)
	{
		auto hostType = [&](const Type* type) { return host.types && host.types->count(QualifiedName(type).c_str()); };
		auto hostSymbol = [&](const Core::String& symbol) { return host.symbols && host.symbols->count(symbol.c_str()); };
		auto hostUnit = [&](uint32 unit) { return unit >= 1 && unit <= host.interfaceUnits; };
		ImageWriter writer(module, model, thunks, errors);
		for (const ObjectType* object : model.ObjectTypes())
			if (Reflected(model, object) && !hostType(object)) writer.AddType(*object);
		for (const EnumType* en : model.EnumTypes())
			if (en && !en->GetName().empty() && !hostType(en)) writer.AddEnum(*en);
		for (const UnionType* un : model.UnionTypes())
			if (un && !un->GetName().empty() && !hostType(un)) writer.AddUnion(*un);

		// A thread_local has no one address, so it is not registered. A name can register twice (a declaration
		// and its definition): the symbol is the identity.
		Names variables; // the symbols' own names, which the model keeps
		model.Symbols()->CollectGlobalVariables([&](const SymbolInfo& variable) {
			if (variable.kind != SymbolKind::Variable || !variable.type || variable.isThreadLocal || variable.mangledName.empty()) return;
			if (hostSymbol(variable.mangledName) || hostUnit(variable.definingUnit)) return;
			if (variables.insert(variable.mangledName.c_str()).second) writer.AddVariable(variable);
		});

		Core::Vector<const FunctionRecord*> records;
		model.AllFunctionRecords(records);
		Names functions;
		functions.reserve(records.Count());
		for (const FunctionRecord* function : records)
			if (ReflectedFunction(*function) && !hostSymbol(function->mangledName) && !hostUnit(function->declUnit) &&
				functions.insert(function->mangledName.c_str()).second)
				writer.AddFunction(*function);
		return writer.Finish();
	}
} // namespace Scripting

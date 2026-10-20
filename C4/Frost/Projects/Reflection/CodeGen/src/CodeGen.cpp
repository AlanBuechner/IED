#include "CodeGen.h"
#include <fstream>
#include <iostream>

static Core::String s_GeneratorName = "";

/// Check if a class has the "observable" attribute flag
bool IsObservable(const Core::Vector<Reflection::Attribute>& attribs)
{
	for (const Reflection::Attribute& attrib : attribs)
	{
		if (attrib.type == Reflection::Attribute::Type::Flag && attrib.name == "observable")
			return true;
	}
	return false;
}

/// Convert container kind string to Reflect::ContainerKind enum literal
Core::String ContainerKindLiteral(const Core::String& kind)
{
	if (kind == "sequential")
		return "Reflect::ContainerKind::Sequential";
	if (kind == "associative")
		return "Reflect::ContainerKind::Associative";
	return "Reflect::ContainerKind::None";
}

/// Get the element type from template args for sequential containers (first arg)
Core::String GetElementType(const Reflection::Property& prop)
{
	if (!prop.m_TemplateArgs.Empty())
		return prop.m_TemplateArgs[0];
	return "";
}

/// Get the key type from template args for associative containers (first arg)
Core::String GetKeyType(const Reflection::Property& prop)
{
	if (!prop.m_TemplateArgs.Empty())
		return prop.m_TemplateArgs[0];
	return "";
}

void ImplementFunctions(std::ofstream& ofs, const Core::Vector<Reflection::Function>& funcs, const Core::String& name, const Core::String& sname)
{
	for (uint32_t i = 0; i < funcs.Count(); i++)
	{
		const Reflection::Function& func = funcs[i];
		ofs << "\tstatic void " << name << func.m_Name << "(void* obj, std::vector<void*> args){";
		for (int i = 0; i < func.m_Types.Count(); i++)
			ofs << func.m_Types[i] << "* v" << i << " = (" << func.m_Types[i] << "*)args[" << i << "];";
		ofs << "((" << sname << "*)obj)->" << func.m_Name << "(";
		for (int i = 0; i < func.m_Types.Count(); i++)
		{
			ofs << "*v" << i;
			if (i != func.m_Types.Count() - 1)
				ofs << ",";
		}
		ofs << ");}" << std::endl;
	}
}

void ImplementOffsets(std::ofstream& ofs, const Core::Vector<Reflection::Property>& props, const Core::String& name, const Core::String& sname)
{
	for (uint32_t i = 0; i < props.Count(); i++)
	{
		const Reflection::Property& prop = props[i];
		ofs << "\tstatic uint64_t " << name << prop.m_Name << "() { return OFFSET_OF(" << sname << ", " << prop.m_Name << "); }" << std::endl;
	}
}

void WriteFlags(std::ofstream& ofs, const Core::Vector<Reflection::Attribute>& attribs)
{
	Core::Vector<Core::String> flags;
	for (const Reflection::Attribute& attrib : attribs)
	{
		if (attrib.type == Reflection::Attribute::Type::Flag)
			flags.Push(attrib.name);
	}
	ofs << "{";
	for (uint32_t i = 0; i < flags.Count(); i++)
	{
		const Core::String& flag = flags[i];
		ofs << "\"" << flag << "\"";
		if (i != flags.Count() - 1)
			ofs << ",";
	}
	ofs << "}";
}

void WriteAttributes(std::ofstream& ofs, const Core::Vector<Reflection::Attribute>& attribs)
{
	Core::Vector<Reflection::Attribute> atributes;
	for (const Reflection::Attribute& attrib : attribs)
	{
		if (attrib.type == Reflection::Attribute::Type::Prop)
			atributes.Push(attrib);
	}
	ofs << "{";
	for (uint32_t i = 0; i < atributes.Count(); i++)
	{
		const Core::String& key = atributes[i].name;
		const Core::String& value = atributes[i].value;
		ofs << "{\"" << key << "\"," << "\"" << value << "\"}";
		if (i != atributes.Count() - 1)
			ofs << ",";
	}
	ofs << "}";
}

void WriteProps(std::ofstream& ofs, const Core::Vector<Reflection::Property> props, const Core::String& name, const Core::String& sname)
{
	ofs << "\t{" << std::endl;
	for (uint32_t i = 0; i < props.Count(); i++)
	{
		const Reflection::Property& prop = props[i];
		ofs << "\t\tReflect::Property(\"" << prop.m_Name << "\"," <<
			"typeid(" << prop.m_Type << ").hash_code()," <<
			"sizeof(" << prop.m_Type << ")," <<
			s_GeneratorName << "::" << name << prop.m_Name << "(),";
			//"offsetof(" << parentTypeName << "," << prop.m_Name << "),";
		WriteFlags(ofs, prop.m_Attributes);
		ofs << ",";
		WriteAttributes(ofs, prop.m_Attributes);
		// Emit path
		ofs << ",\"" << prop.m_Path << "\"";
		// Emit container kind enum
		ofs << "," << ContainerKindLiteral(prop.m_ContainerKind);
		// Emit element type ID (for sequential containers)
		if (prop.m_ContainerKind == "sequential")
		{
			Core::String elemType = GetElementType(prop);
			ofs << ",typeid(" << elemType << ").hash_code()";
		}
		else
		{
			ofs << ",0";
		}
		// Emit key type ID (for associative containers)
		if (prop.m_ContainerKind == "associative")
		{
			Core::String keyType = GetKeyType(prop);
			ofs << ",typeid(" << keyType << ").hash_code()";
		}
		else
		{
			ofs << ",0";
		}
		ofs << ")";
		if (i != props.Count() - 1)
			ofs << ",";
		ofs << std::endl;
	}
	ofs << "\t}";
}

void WriteFuncs(std::ofstream& ofs, const Core::Vector<Reflection::Function>& funcs, const Core::String& name, const Core::String& sname)
{
	ofs << "\t{" << std::endl;
	for (uint32_t i = 0; i < funcs.Count(); i++)
	{
		const Reflection::Function& func = funcs[i];
		ofs << "\t\tReflect::Function(\"" << func.m_Name << "\",";
		ofs << s_GeneratorName << "::" << name << func.m_Name;
		ofs << ",";
		WriteFlags(ofs, func.m_Attributes);
		ofs << ",";
		WriteAttributes(ofs, func.m_Attributes);
		ofs << ")";
		if (i != funcs.Count() - 1)
			ofs << ",";
		ofs << std::endl;
	}
	ofs << "\t}";
}

/// Generate static subscription hooks for observable classes.
/// These hooks subscribe each reflected property's path to the ChangeQueue
/// so that change notifications are automatically routed.
void WriteChangeHooks(std::ofstream& ofs, const Reflection::Class& c)
{
	const Core::String& sname = c.m_SemanticName;
	const Core::String& name = c.m_Name;

	ofs << "\t// ChangeQueue subscription hooks for " << sname << std::endl;

	// Conditional include for ChangeNotification
	ofs << "\t#if __has_include(<ChangeNotification/ChangeNotification.h>)" << std::endl;
	ofs << "\t#include <ChangeNotification/ChangeNotification.h>" << std::endl;
	ofs << "\t#else" << std::endl;
	ofs << "\t#error ChangeNotification header not found for observable class " << sname << std::endl;
	ofs << "\t#endif" << std::endl;

	// Static subscription IDs (one per property)
	for (uint32_t i = 0; i < c.m_Props.Count(); i++)
	{
		const Reflection::Property& prop = c.m_Props[i];
		ofs << "\tstatic uint32_t s_" << name << "_" << prop.m_Name << "_SubID = 0;" << std::endl;
	}

	// Static init function that subscribes to ChangeQueue for each property path
	ofs << "\tstatic void " << name << "SubscribeChangeHooks()" << std::endl;
	ofs << "\t{" << std::endl;
	for (uint32_t i = 0; i < c.m_Props.Count(); i++)
	{
		const Reflection::Property& prop = c.m_Props[i];
		ofs << "\t\ts_" << name << "_" << prop.m_Name << "_SubID = ChangeNotification::ChangeQueue::GetInstance().Subscribe([](const ChangeNotification::ChangeRecord& record) {" << std::endl;
		ofs << "\t\t\t// Change notification for " << prop.m_Path << std::endl;
		ofs << "\t\t}, \"" << prop.m_Path << "\");" << std::endl;
	}
	ofs << "\t}" << std::endl;

	// Static invoker that calls SubscribeChangeHooks at program startup
	ofs << "\tstatic struct " << name << "ChangeHookInit {" << std::endl;
	ofs << "\t\t" << name << "ChangeHookInit() { " << name << "SubscribeChangeHooks(); }" << std::endl;
	ofs << "\t} s_" << name << "ChangeHookInit;" << std::endl;
}

void WriteCode(const fs::path& path, const Core::String& projectName, const Reflection::ReflectionData& data) {

	fs::remove(path);
	std::ofstream ofs(path);

	// generate reflection code
	ofs << "#include \"Reflection.h\"" << std::endl;
	ofs << "#include <stdarg.h>" << std::endl;

	bool hasObservable = false;
	for (const auto& c : data.m_Classes)
	{
		if (IsObservable(c.m_Attributes))
		{
			hasObservable = true;
			break;
		}
	}

	if (hasObservable)
	{
		ofs << "#if __has_include(<ChangeNotification/ChangeNotification.h>)" << std::endl;
		ofs << "#include <ChangeNotification/ChangeNotification.h>" << std::endl;
		ofs << "#endif" << std::endl;
	}

	//ofs << "#define _XKEYCHECK_H" << std::endl;
	//ofs << "#define private public" << std::endl;
	//ofs << "#define protected public" << std::endl;
	for (auto& h : data.m_Headers)
		ofs << "#include \"" << h << "\"" << std::endl;

	ofs << "#define OFFSET_OF(s,m) ((size_t)&(((s*)0)->m))" << std::endl;
	//ofs << "#define OFFSET_OF(s,m) ((::size_t)&reinterpret_cast<char const volatile&>((((s*)0)->m)))" << std::endl;
	//ofs << "#define OFFSET_OF(s,m) offsetof(s,m)" << std::endl;

	s_GeneratorName = projectName + "Generator";
	ofs << "class " << s_GeneratorName << " {" << std::endl;
	ofs << "public:" << std::endl;

	for (auto& c : data.m_Classes) {

		const Core::String& sname = c.m_SemanticName;
		const Core::String& name = c.m_Name;
		const Core::String& group = c.m_Group;
		const Core::Vector<Reflection::Attribute>& attribs = c.m_Attributes;
		const Core::Vector<Reflection::Property>& props = c.m_Props;
		const Core::Vector<Reflection::Function>& funcs = c.m_Functions;

		ImplementFunctions(ofs, funcs, name, sname);
		ImplementOffsets(ofs, props, name, sname);
	}

	ofs << "};" << std::endl;


	for (auto& c : data.m_Classes) {

		const Core::String& sname = c.m_SemanticName;
		const Core::String& name = c.m_Name;
		const Core::String& group = c.m_Group;
		const Core::String& scriptingLanguageID = c.m_ScriptingLanguageID;
		const Core::Vector<Reflection::Attribute>& attribs = c.m_Attributes;
		const Core::Vector<Reflection::Property>& props = c.m_Props;
		const Core::Vector<Reflection::Function>& funcs = c.m_Functions;

		ofs << "static Reflect::Registry::Add<" << sname << "> Class" << name << "(" << std::endl
			<< "\t\"" << name << "\", \"" << sname << "\", \"" << group << "\", \"" << scriptingLanguageID << "\"," << std::endl;
		
		ofs << "\t";
		WriteFlags(ofs, attribs);
		ofs << "," << std::endl;
		ofs << "\t";
		WriteAttributes(ofs, attribs);
		ofs << "," << std::endl;
		WriteProps(ofs, props, name, sname);
		ofs << "," << std::endl;
		WriteFuncs(ofs, funcs, name, sname);
		ofs << ");" << std::endl;

		// Generate change hooks for observable classes
		if (IsObservable(attribs))
		{
			WriteChangeHooks(ofs, c);
		}
	}

	ofs << "void DeadLink" << projectName << "() {}" << std::endl;
	ofs.close();
}

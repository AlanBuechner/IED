#include "ReflectionData.h"
#include "Utils.h"
#include <queue>
#include <fstream>

#include "ObjectDescription/YAMLObjectDescription.h"

namespace Reflection
{
	Core::Vector<Attribute> ParseAttributeString(const Core::String& attribString) {
		Core::Vector<Attribute> attrs;
		Core::String attribs = attribString;
		attribs.erase(remove(attribs.begin(), attribs.end(), ' '), attribs.end());

		Core::Vector<char> reservedTokens = { ',', '=' };
		std::queue<Core::String> tokens = Tokenize(attribs, reservedTokens);

		while (!tokens.empty()) {
			Core::String name = tokens.front();
			tokens.pop(); // remove name

			if (tokens.empty() || tokens.front() == ",") { // flag
				if (!tokens.empty())
					tokens.pop();
				if (name != "")
					attrs.Push({ Attribute::Flag, name, "" });
			}
			else if (tokens.front() == "=") {
				tokens.pop(); // remove "="
				if (name != "")
					attrs.Push({ Attribute::Prop, name, tokens.front() });
				tokens.pop(); // remove value
				// remove ","
				if (tokens.front() == ",")
					tokens.pop();
			}
		}

		return attrs;
	}

	Core::String FormatAttributeString(const Core::Vector<Attribute>& attribs)
	{
		Core::String attrs;

		for (const Attribute& attrib : attribs)
		{
			if (attrs != "")
				attrs += ",";
			attrs += attrib.name;
			if (attrib.type == Attribute::Prop)
				attrs += "=" + attrib.value;
		}

		return attrs;
	}


	// --------------------------- Reflected Property --------------------------- //
	Property::Property(const Core::String& name, const Core::String& type, const Core::String& attrib) :
		m_Name(name), m_Type(type)
	{
		m_Attributes = ParseAttributeString(attrib);
	}

	Core::ObjectDescription Property::Save() const
	{
		Core::ObjectDescription desc;
		desc["Name"] = Core::ObjectDescription::CreateFrom(m_Name);
		desc["Type"] = Core::ObjectDescription::CreateFrom(m_Type);
		desc["Attributes"] = Core::ObjectDescription::CreateFrom(FormatAttributeString(m_Attributes));

		if (m_Path != "")
			desc["Path"] = Core::ObjectDescription::CreateFrom(m_Path);

		if (!m_TemplateArgs.Empty())
			desc["TemplateArgs"] = Core::ObjectDescription::CreateFrom(m_TemplateArgs);

		if (m_ContainerKind != "")
			desc["ContainerKind"] = Core::ObjectDescription::CreateFrom(m_ContainerKind);

		return desc;
	}

	Property Property::Load(const Core::ObjectDescription& desc)
	{
		Property p = Property(desc["Name"].GetAsString(), desc["Type"].GetAsString(), desc["Attributes"].GetAsString());

		if (desc.HasEntery("Path"))
			p.m_Path = desc["Path"].GetAsString();

		if (desc.HasEntery("TemplateArgs"))
			p.m_TemplateArgs = desc["TemplateArgs"].Get<Core::Vector<Core::String>>();

		if (desc.HasEntery("ContainerKind"))
			p.m_ContainerKind = desc["ContainerKind"].GetAsString();

		return p;
	}


	// --------------------------- Reflected Function --------------------------- //
	Function::Function(const Core::String& name, const Core::Vector<Core::String>& types, const Core::String& attrib) :
		m_Name(name), m_Types(types)
	{
		m_Attributes = ParseAttributeString(attrib);
	}

	Core::ObjectDescription Function::Save() const
	{
		Core::ObjectDescription desc;
		desc["Name"] = Core::ObjectDescription::CreateFrom(m_Name);
		desc["Types"] = Core::ObjectDescription::CreateFrom(m_Types);
		desc["Attributes"] = Core::ObjectDescription::CreateFrom(FormatAttributeString(m_Attributes));
		return desc;
	}

	Function Function::Load(const Core::ObjectDescription& desc)
	{
		Core::Vector<Core::String> types = desc["Types"].Get<Core::Vector<Core::String>>();

		Function f = Function(desc["Name"].GetAsString(), types, desc["Attributes"].GetAsString());

		return f;
	}



	// --------------------------- Reflected Class --------------------------- //
	Class::Class(const Core::String& name, const Core::String& attrib)
		: m_SemanticName(name) {
		m_Attributes = ParseAttributeString(attrib);
		m_Name = GetName();
		m_Group = GetGroup();
		m_ScriptingLanguageID = GetScriptingLanguageID();
	}

	Core::ObjectDescription Class::Save() const
	{
		Core::ObjectDescription desc;
		desc["Name"] = Core::ObjectDescription::CreateFrom(m_SemanticName);
		desc["Attributes"] = Core::ObjectDescription::CreateFrom(FormatAttributeString(m_Attributes));

		if (m_ScriptingLanguageID != "")
			desc["ScriptingLanguageID"] = Core::ObjectDescription::CreateFrom(m_ScriptingLanguageID);

		if (!m_TemplateArgs.Empty())
			desc["TemplateArgs"] = Core::ObjectDescription::CreateFrom(m_TemplateArgs);

		// write out props
		Core::ObjectDescription propDesc = Core::ObjectDescription(Core::ObjectDescription::Type::Array);
		for (const Property& c : m_Props)
			propDesc.Push(c.Save());
		desc["Props"] = propDesc;

		// write out functions
		Core::ObjectDescription funcDesc = Core::ObjectDescription(Core::ObjectDescription::Type::Array);
		for (const Function& c : m_Functions)
			funcDesc.Push(c.Save());
		desc["Functions"] = funcDesc;

		return desc;
	}

	Class Class::Load(const Core::ObjectDescription& desc)
	{
		Class c(desc["Name"].GetAsString(), desc["Attributes"].GetAsString());

		// load scripting language ID if present
		if (desc.HasEntery("ScriptingLanguageID"))
			c.m_ScriptingLanguageID = desc["ScriptingLanguageID"].GetAsString();

		if (desc.HasEntery("TemplateArgs"))
			c.m_TemplateArgs = desc["TemplateArgs"].Get<Core::Vector<Core::String>>();

		// load props
		const Core::Vector<Core::ObjectDescription>& props = desc["Props"].GetAsObjectArray();
		for (const Core::ObjectDescription& p : props)
			c.m_Props.Push(Property::Load(p));

		// load functions
		const Core::Vector<Core::ObjectDescription>& functions = desc["Functions"].GetAsObjectArray();
		for (const Core::ObjectDescription& f : functions)
			c.m_Functions.Push(Function::Load(f));

		return c;
	}

	Core::String Class::GetGroup() {
		for (auto& a : m_Attributes) {
			if (a.type == Attribute::Prop && a.name == "GROUP") {
				return a.value;
			}
		}
		return "";
	}

	Core::String Class::GetName() {
		uint32_t index = 0;
		for (int32 i = m_SemanticName.size() - 1; i >= 0; i--) {
			if (m_SemanticName[i] == ':') {
				index = i + 1;
				break;
			}
		}

		return m_SemanticName.substr(index);
	}

	Core::String Class::GetScriptingLanguageID() {
		for (auto& a : m_Attributes) {
			if (a.type == Attribute::Prop && a.name == "ScriptingLanguageID") {
				return a.value;
			}
		}
		return "";
	}



	// --------------------------- Reflected File --------------------------- //
	void File::Save(fs::path filepath) const
	{
		Core::ObjectDescription desc;

		desc["Header"] = Core::ObjectDescription::CreateFrom(m_Header);
		Core::ObjectDescription classDesc = Core::ObjectDescription(Core::ObjectDescription::Type::Array);
		for (const Class& c : m_Classes)
			classDesc.Push(c.Save());
		desc["Classes"] = classDesc;

		std::cout << "reflection info found in file " << desc["Header"].GetAsString() << std::endl;

		YAML::Emitter out;
		Core::WriteObjectDescriptionToYAML(desc, out);
		fs::create_directories(filepath.parent_path());
		std::ofstream fout(filepath);
		fout << out.c_str();
		fout.flush();
		fout.close();
	}

	File File::Load(fs::path filepath)
	{
		File f;

		Core::ObjectDescription desc = Core::LoadObjectDescriptionFromYAMLFile(filepath);
		f.m_Header = desc["Header"].GetAsString();
		Core::Vector<Core::ObjectDescription>& classes = desc["Classes"].GetAsObjectArray();
		for (Core::ObjectDescription& c : classes)
			f.m_Classes.Push(Class::Load(c));

		return f;
	}

	void ReflectionData::Collect()
	{
		for (const File& file : m_Files)
		{
			m_Headers.Push(file.m_Header);
			for (const Class& c : file.m_Classes)
				m_Classes.Push(c);
		}
	}

	void ReflectionData::LoadFolder(fs::path folder)
	{
		for (auto& p : fs::directory_iterator(folder)) {
			if (p.is_directory())
				continue;

			fs::path path = p.path();
			if (path.extension() != ".reflected")
				continue;

			LoadFile(path);
		}
	}

}

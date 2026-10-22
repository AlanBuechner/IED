#pragma once

#include "Core/Core.h"
#include "ObjectDescription/ObjectDescription.h"

namespace Reflection
{
	struct Attribute {
		enum Type {
			Flag,
			Prop,
		} type;
		Core::String name;
		Core::String value;
	};

	// --------------------------- Reflected Property --------------------------- //
	class Property
	{
	public:
		Property(const Core::String& name, const Core::String& type, const Core::String& attrib);
		Core::String m_Name;
		Core::String m_Type;
		Core::String m_Path;
		Core::Vector<Core::String> m_TemplateArgs;
		Core::String m_ContainerKind;
		Core::Vector<Attribute> m_Attributes;

		Core::ObjectDescription Save() const;
		static Property Load(const Core::ObjectDescription& desc);
	};

	// --------------------------- Reflected Function --------------------------- //
	class Function
	{
	public:
		Function(const Core::String& name, const Core::Vector<Core::String>& types, const Core::String& attrib);
		Core::String m_Name;
		Core::Vector<Core::String> m_Types;
		Core::Vector<Attribute> m_Attributes;

		Core::ObjectDescription Save() const;
		static Function Load(const Core::ObjectDescription& desc);
	};

	// --------------------------- Reflected Class --------------------------- //
	class Class {
	public:
		Class() = default;
		Class(const Core::String& name, const Core::String& attrib);
		Core::String m_Name;
		Core::String m_Group;
		Core::String m_SemanticName;
		Core::String m_ScriptingLanguageID;
		Core::Vector<Core::String> m_TemplateArgs;
		Core::Vector<Attribute> m_Attributes;
		Core::Vector<Property> m_Props;
		Core::Vector<Function> m_Functions;

		Core::ObjectDescription Save() const;
		static Class Load(const Core::ObjectDescription& desc);

	private:
		Core::String GetGroup();
		Core::String GetName();
		Core::String GetScriptingLanguageID();
	};

	// --------------------------- Reflected File --------------------------- //
	class File
	{
	public:
		File() = default;
		File(Core::String header) : m_Header(header) {}

		Core::String m_Header;
		Core::Vector<Class> m_Classes;

		void Save(fs::path filepath) const;
		static File Load(fs::path filepath);
	};


	class ReflectionData
	{
	public:
		Core::Vector<File> m_Files;

		Core::Vector<Core::String> m_Headers;
		Core::Vector<Class> m_Classes;
		
		void Collect();

		void LoadFile(fs::path filePath) { m_Files.Push(File::Load(filePath)); }
		void LoadFolder(fs::path folder);
	};
}
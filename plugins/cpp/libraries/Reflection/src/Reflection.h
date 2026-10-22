#pragma once


#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <vector>
#include <iostream>

namespace Reflect {

	/// Container classification for reflected properties.
	enum class ContainerKind : uint8_t {
		None = 0,
		Sequential = 1,   // Core::Vector, std::vector, std::list
		Associative = 2   // std::map, std::unordered_map
	};

	template <class T>
	T* CreateInstance(void* location = nullptr) {
		if (location == nullptr)
			return new T();
		return new(location) T;
	}

	template <class T>
	void DestroyInstance(void* location, bool freeMemory) {
		if (location != nullptr)
		{
			if(freeMemory)
				delete (T*)location;
			else
				((T*)location)->~T();
		}

	}

	class Class;

	/// Unique handle for a callback subscription, used to unsubscribe.
	using CallbackHandle = uint64_t;

	/// Event fired when reflection data for a scripting language is about to be unloaded.
	/// The callback receives the scripting language ID being unloaded.
	using UnloadHandler = std::function<void(const std::string& scriptingLanguageID)>;

	/// Event fired after new reflection data has been registered (reload complete).
	/// The callback receives the scripting language ID that was reloaded, or empty string for general reloads.
	using ReloadHandler = std::function<void(const std::string& scriptingLanguageID)>;

	class Attribute
	{
	public:
		Attribute() = default;
		Attribute(const std::string& key, const std::string& value) :
			key(key), value(value)
		{}

		const std::string& GetKey() const { return key; }
		const std::string& GetValue() const { return value; }

	private:
		std::string key;
		std::string value;
	};

	class Property
	{
	public:
		Property() = default;
		Property(
			const std::string& name,
			uint64_t typeID,
			uint32_t size,
			uint32_t offset,
			const std::vector<std::string>& flags,
			const std::vector<Attribute>& attributes,
			const std::string& path = "",
			ContainerKind containerKind = ContainerKind::None,
			uint64_t elementTypeID = 0,
			uint64_t keyTypeID = 0
		) :
			name(name),
			typeID(typeID),
			size(size),
			offset(offset),
			path(path),
			containerKind(containerKind),
			elementTypeID(elementTypeID),
			keyTypeID(keyTypeID),
			attributes(attributes)
		{
			for (const std::string& flag : flags)
				this->flags.emplace(flag);
			for (uint32_t i = 0; i < attributes.size(); i++)
				attributeMap.emplace(attributes[i].GetKey(), i);
		}

		const std::string& GetName() const { return name; }
		uint64_t GetTypeID() const { return typeID; }
		uint32_t GetSize() const { return size; }
		uint32_t GetOffset() const { return offset; }
		bool HasFlag(const std::string& flag) const { return flags.find(flag) != flags.end(); }

		const std::vector<Attribute>& GetAttributes() const { return attributes; }
		bool HasAttribute(const std::string& key) const { return attributeMap.find(key) != attributeMap.end(); }
		const Attribute& GetAttribute(const std::string& key) const { return attributes[attributeMap.at(key)]; }

		const std::string& GetPath() const { return path; }
		ContainerKind GetContainerKind() const { return containerKind; }
		uint64_t GetElementTypeID() const { return elementTypeID; }
		uint64_t GetKeyTypeID() const { return keyTypeID; }

		const Class& GetClass() const { return *c; }

	private:
		std::string name = "";
		uint64_t typeID = 0;
		uint32_t size = 0;
		uint32_t offset = 0;

		std::unordered_set<std::string> flags;

		std::vector<Attribute> attributes;
		std::unordered_map<std::string, uint32_t> attributeMap;

		std::string path;
		ContainerKind containerKind = ContainerKind::None;
		uint64_t elementTypeID = 0; // For sequential containers: typeid of element type
		uint64_t keyTypeID = 0;     // For associative containers: typeid of key type

		Class* c;
		friend Class;
		friend class Registry;
	};

	class Function
	{
	public:
		typedef std::function<void (void*, std::vector<void*>)> InvokeFunc;

		Function() = default;
		Function(
			const std::string& name,
			InvokeFunc func,
			const std::vector<std::string>& flags,
			const std::vector<Attribute>& attributes
		) :
			m_Name(name),
			m_Function(func),
			m_Attributes(attributes)
		{
			for (const std::string& flag : flags)
				this->m_Flags.emplace(flag);
			for (uint32_t i = 0; i < attributes.size(); i++)
				m_AttributeMap.emplace(attributes[i].GetKey(), i);
		}

		const std::string& GetName() const { return m_Name; }

		void Invoke(void* obj, std::vector<void*> args) const { m_Function(obj, args); }

		bool HasFlag(const std::string& flag) const { return m_Flags.find(flag) != m_Flags.end(); }

		const std::vector<Attribute>& GetAttributes() const { return m_Attributes; }
		bool HasAttribute(const std::string& key) const { return m_AttributeMap.find(key) != m_AttributeMap.end(); }
		const Attribute& GetAttribute(const std::string& key) const { return m_Attributes[m_AttributeMap.at(key)]; }

	private:
		std::string m_Name;
		InvokeFunc m_Function;

		std::unordered_set<std::string> m_Flags;

		std::vector<Attribute> m_Attributes;
		std::unordered_map<std::string, uint32_t> m_AttributeMap;
		Class* c;
		friend Class;
		friend class Registry;
	};

	class Class {
	public:
		typedef std::function<void* (void*)> CreateFunc;
		typedef std::function<void (void*, bool)> DestroyFunc;

		Class() = default;
		Class(
			const std::string& name,
			const std::string& sname,
			uint64_t typeID,
			uint64_t size,
			const std::string& group,
			const std::string& scriptingLanguageID,
			CreateFunc createFunc,
			DestroyFunc destroyFunc,
			const std::vector<std::string>& flags,
			const std::vector<Attribute>& attribs,
			const std::vector<Property>& props,
			const std::vector<Function>& funcs
		):
			name(name),
			sname(sname),
			typeID(typeID),
			size(size),
			group(group),
			scriptingLanguageID(scriptingLanguageID),
			createFunc(createFunc),
			destroyFunc(destroyFunc),
			attributes(attribs),
			properties(props),
			functions(funcs)
		{
			for (const std::string& flag : flags)
				this->flags.emplace(flag);
			for (uint32_t i = 0; i < attributes.size(); i++)
				attributeMap.emplace(attributes[i].GetKey(), i);
			for (uint32_t i = 0; i < properties.size(); i++)
				propertyMap.emplace(properties[i].GetName(), i);
			for (uint32_t i = 0; i < functions.size(); i++)
				functionMap.emplace(functions[i].GetName(), i);
		}

		const std::string& GetName() const { return name; }
		const std::string& GetSname() const { return sname; }
		const uint64_t GetTypeID() const { return typeID; }
		const uint64_t GetSize() const { return size; }
		const std::string& GetGroup() const { return group; }
		const std::string& GetScriptingLanguageID() const { return scriptingLanguageID; }

		void* CreateInstance(void* location = nullptr) const { return createFunc(location); }
		void DestroyInstance(void* location, bool freeMemory = true) const { destroyFunc(location, freeMemory); }

		bool HasFlag(const std::string& flag) const { return flags.find(flag) != flags.end(); }

		const std::vector<Attribute>& GetAttributes() const { return attributes; }
		bool HasAttribute(const std::string& key) const { return attributeMap.find(key) != attributeMap.end(); }
		const Attribute& GetAttribute(const std::string& key) const { return attributes[attributeMap.at(key)]; }

		const std::vector<Function>& GetFunctions() const { return functions; }
		bool HasFunction(const std::string& name) const { return functionMap.find(name) != functionMap.end(); }
		const Function& GetFunction(const std::string& name) const { return functions[functionMap.at(name)]; }

		const std::vector<Property>& GetProperties() const { return properties; }
		bool HasProperty(const std::string& name) { return propertyMap.find(name) != propertyMap.end(); }
		const Property& GetProperty(const std::string& name) const { return properties[propertyMap.at(name)]; }

	private:
		std::string name;
		std::string sname;
		uint64_t typeID;
		uint64_t size;
		std::string group;
		std::string scriptingLanguageID;
		CreateFunc createFunc;
		DestroyFunc destroyFunc;
		std::unordered_set<std::string> flags;

		std::vector<Attribute> attributes;
		std::unordered_map<std::string, uint32_t> attributeMap;

		std::vector<Property> properties;
		std::unordered_map<std::string, uint32_t> propertyMap;

		std::vector<Function> functions;
		std::unordered_map<std::string, uint32_t> functionMap;
		friend class Registry;
	};

	// Forward declarations for Scripting namespace friend functions (defined in ReflectionEvents.h/cpp)
	namespace Scripting {
		CallbackHandle SubscribeUnload(const UnloadHandler& handler);
		CallbackHandle SubscribeReload(const ReloadHandler& handler);
		void Unsubscribe(CallbackHandle handle);
		void UnloadScriptingReflection(const std::string& scriptingLanguageID);
		void InvokeReloadCallbacks(const std::string& scriptingLanguageID);
		const std::vector<const Class*>& GetClassesByScriptingLanguageID(const std::string& scriptingLanguageID);
		bool HasScriptingLanguage(const std::string& scriptingLanguageID);
	}

	class Registry {
		static Registry* s_Instance;

	public:
		static Registry* GetRegistry();

		const Class* GetClass(const std::string& name) { return m_ClassesByName[name]; }
		const Class* GetClass(uint64_t typeID) { return m_ClassesByTypeID[typeID]; }
		const std::list<Class>& GetClasses() { return m_Classes; }
		const std::vector<const Class*>& GetGroup(const std::string& groupName) { return m_ClassGroups[groupName]; }

		void AddClass(const Class& c);

		template <class T> class Add {
		public:
			Add(const std::string& name,
				const std::string& sname,
				const std::string& group,
				const std::string& scriptingLanguageID,
				const std::vector<std::string>& flags,
				const std::vector<Attribute>& attributes,
				const std::vector<Property>& props,
				const std::vector<Function>& funcs
			){
				Registry::GetRegistry()->AddClass(Class(
					name,
					sname,
					typeid(T).hash_code(),
					sizeof(T),
					group,
					scriptingLanguageID,
					CreateInstance<T>,
					DestroyInstance<T>,
					flags,
					attributes,
					props,
					funcs
				));
			}
		};

	private:
		std::unordered_map<std::string, const Class*> m_ClassesByName;
		std::unordered_map<uint64_t, const Class*> m_ClassesByTypeID;
		std::unordered_map<std::string, std::vector<const Class*>> m_ClassGroups;
		std::list<Class> m_Classes;

		/// Index of classes by scripting language ID. Only populated for non-empty IDs.
		std::unordered_map<std::string, std::vector<const Class*>> m_ClassesByScriptingLanguageID;

		// --- Callback subscription infrastructure ---
		static CallbackHandle s_NextCallbackHandle;
		std::unordered_map<CallbackHandle, UnloadHandler> m_UnloadCallbacks;
		std::unordered_map<CallbackHandle, ReloadHandler> m_ReloadCallbacks;

		// Friend declarations for Scripting namespace functions that need private access
		friend CallbackHandle Scripting::SubscribeUnload(const UnloadHandler& handler);
		friend CallbackHandle Scripting::SubscribeReload(const ReloadHandler& handler);
		friend void Scripting::Unsubscribe(CallbackHandle handle);
		friend void Scripting::UnloadScriptingReflection(const std::string& scriptingLanguageID);
		friend void Scripting::InvokeReloadCallbacks(const std::string& scriptingLanguageID);
		friend const std::vector<const Class*>& Scripting::GetClassesByScriptingLanguageID(const std::string& scriptingLanguageID);
		friend bool Scripting::HasScriptingLanguage(const std::string& scriptingLanguageID);
	};

	class Reflected
	{
	public:
		virtual const Reflect::Class& GetClass() = 0;
		virtual const std::string& GetTypeName() = 0;
		virtual uint64_t GetTypeID() = 0;
	};

	/// Base class for observable objects. Provides ChangeQueue subscription hooks
	/// that are generated by CodeGen for each CLASS_OBSERVABLE type.
	class ObservableBase : public Reflected
	{
	public:
		virtual ~ObservableBase() = default;

		/// Returns the object's path for change notifications (e.g. "Namespace.ClassName")
		virtual const std::string& GetObjectPath() const = 0;

		/// Returns a pointer to this instance for use in ChangeRecord.ObjectPointer
		virtual void* GetObjectPointer() = 0;
	};

} // namespace Reflect

#define REFLECT_TOKEN_PASTE(x, y) x##y
#define REFLECT_CAT(x,y) REFLECT_TOKEN_PASTE(x,y)

// requires the full semantic name of the class i.e. (SomeNamespace::SomeClass)
#define REFLECTED_BODY(typeName)	\
								static const Reflect::Class& GetStaticClass() { static auto& c = *(Reflect::Registry::GetRegistry()->GetClass(typeid(typeName).hash_code())); return c; }\
								static uint64_t GetStaticTypeID() { static uint64 tid = GetStaticClass().GetTypeID(); return tid; }\
								virtual const Reflect::Class& GetClass() override { return GetStaticClass(); }\
								virtual const std::string& GetTypeName() override { return GetStaticClass().GetSname(); }\
								virtual uint64_t GetTypeID() override { return GetStaticTypeID(); }\
								friend class ::REFLECT_CAT(PROJECT_NAME,Generator); /* PROJECT_NAME only needs to be defined when compiling the generated.cpp for this to work */ \

// STRIP_CLANG is used so the IDE does not yell at you because it does not know what __attribute__ is
#if defined(STRIP_CLANG)
	#define CLASS(...) class
	#define PROPERTY(...)
	#define FUNCTION(...)
	#define CLASS_OBSERVABLE(...) class
#else
	#define CLASS(...) class __attribute__((annotate("reflect-class," #__VA_ARGS__)))
	#define PROPERTY(...) __attribute__((annotate("reflect-property," #__VA_ARGS__)))
	#define FUNCTION(...) __attribute__((annotate("reflect-function," #__VA_ARGS__)))
	#define CLASS_OBSERVABLE(...) class __attribute__((annotate("reflect-observable," #__VA_ARGS__)))
#endif

#define LINK_REFLECTION_DATA void REFLECT_CAT(DeadLink,PROJECT_NAME()); void* REFLECT_CAT(_DeadLink, PROJECT_NAME) = (void*)&REFLECT_CAT(DeadLink,PROJECT_NAME);

#define DEC_GENERATOR class REFLECT_CAT(PROJECT_NAME,Generator);
DEC_GENERATOR;

#include "ReflectionEvents.h"
#include <filesystem>
#include <sstream>
#include <numeric>
#include <iomanip>
#include <algorithm>
#include <unordered_set>

namespace fs = std::filesystem;

namespace Reflect {

	std::string ComputeScriptingLanguageID(const std::vector<std::string>& sourcePaths)
	{
		if (sourcePaths.empty())
			return "";

		// Build a combined hash from file paths and their last write times.
		// This produces a stable ID that changes when any source file is modified.
		uint64_t hash = 0;
		for (const std::string& path : sourcePaths)
		{
			std::hash<std::string> hasher;
			hash ^= hasher(path);

			try {
				auto lastWrite = fs::last_write_time(fs::path(path));
				auto timeVal = lastWrite.time_since_epoch().count();
				hash ^= static_cast<uint64_t>(timeVal);
			} catch (...) {
				// File doesn't exist or can't be stat'd; ignore.
			}
		}

		std::ostringstream oss;
		oss << "scripting-" << std::hex << hash;
		return oss.str();
	}

	// --- Scripting namespace implementation ---

	namespace Scripting {

		CallbackHandle SubscribeUnload(const UnloadHandler& handler)
		{
			auto* registry = Registry::GetRegistry();
			auto handle = Registry::s_NextCallbackHandle++;
			registry->m_UnloadCallbacks[handle] = handler;
			return handle;
		}

		CallbackHandle SubscribeReload(const ReloadHandler& handler)
		{
			auto* registry = Registry::GetRegistry();
			auto handle = Registry::s_NextCallbackHandle++;
			registry->m_ReloadCallbacks[handle] = handler;
			return handle;
		}

		void Unsubscribe(CallbackHandle handle)
		{
			auto* registry = Registry::GetRegistry();
			registry->m_UnloadCallbacks.erase(handle);
			registry->m_ReloadCallbacks.erase(handle);
		}

	void UnloadScriptingReflection(const std::string& scriptingLanguageID)
	{
		auto* registry = Registry::GetRegistry();

		// 1. Invoke unload callbacks first (collect into a vector to avoid mutation during iteration)
		std::vector<UnloadHandler> callbacksToInvoke;
		for (const auto& pair : registry->m_UnloadCallbacks)
			callbacksToInvoke.push_back(pair.second);

		for (auto& cb : callbacksToInvoke)
			cb(scriptingLanguageID);

		// 2. Find matching classes in the scripting language index
		auto langIt = registry->m_ClassesByScriptingLanguageID.find(scriptingLanguageID);
		if (langIt == registry->m_ClassesByScriptingLanguageID.end())
			return;

		// Collect class pointers, semantic names, and type IDs before modifying any indices.
		// These pointers remain valid as long as we don't modify m_Classes yet.
		std::vector<const Class*> classesToUnload;
		std::vector<std::string> classNamesToRemove;
		for (const Class* cls : langIt->second) {
			classesToUnload.push_back(cls);
			classNamesToRemove.push_back(cls->GetSname());
		}

		// 3. Clear the scripting language index entry
		langIt->second.clear();

		// 4. Remove from primary name index
		for (const std::string& sname : classNamesToRemove) {
			registry->m_ClassesByName.erase(sname);
		}

		// 5. Remove from type ID index
		for (const Class* cls : classesToUnload) {
			registry->m_ClassesByTypeID.erase(cls->GetTypeID());
		}

		// 6. Remove from group indices (must happen before m_Classes modification since groups hold pointers into m_Classes)
		std::unordered_set<const Class*> classSet(classesToUnload.begin(), classesToUnload.end());
		for (auto& groupPair : registry->m_ClassGroups) {
			auto& groupVec = groupPair.second;
			groupVec.erase(
				std::remove_if(groupVec.begin(), groupVec.end(),
					[&classSet](const Class* cls) {
						return classSet.count(cls) > 0;
					}),
				groupVec.end()
			);
		}

		// 7. Remove from m_Classes list last (this may invalidate pointers, so all pointer-based cleanup is done above)
		registry->m_Classes.erase(
			std::remove_if(registry->m_Classes.begin(), registry->m_Classes.end(),
				[scriptingLanguageID](const Class& cls) {
					return cls.GetScriptingLanguageID() == scriptingLanguageID;
				}),
			registry->m_Classes.end()
		);
	}

		void InvokeReloadCallbacks(const std::string& scriptingLanguageID)
		{
			auto* registry = Registry::GetRegistry();

			// Collect into a vector to avoid mutation during iteration
			std::vector<ReloadHandler> callbacksToInvoke;
			for (const auto& pair : registry->m_ReloadCallbacks)
				callbacksToInvoke.push_back(pair.second);

			for (auto& cb : callbacksToInvoke)
				cb(scriptingLanguageID);
		}

		const std::vector<const Class*>& GetClassesByScriptingLanguageID(const std::string& scriptingLanguageID)
		{
			auto* registry = Registry::GetRegistry();
			return registry->m_ClassesByScriptingLanguageID[scriptingLanguageID];
		}

		bool HasScriptingLanguage(const std::string& scriptingLanguageID)
		{
			auto* registry = Registry::GetRegistry();
			auto it = registry->m_ClassesByScriptingLanguageID.find(scriptingLanguageID);
			return it != registry->m_ClassesByScriptingLanguageID.end() && !it->second.empty();
		}

	} // namespace Scripting

} // namespace Reflect

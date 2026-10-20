#pragma once

#include "Reflection.h"
#include <functional>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>

namespace Reflect {

	/// Subscription record tracking an active callback registration.
	struct CallbackSubscription {
		CallbackHandle handle;
		UnloadHandler unloadCallback;
		ReloadHandler reloadCallback;
	};

	/// Compute a unique scripting language ID from a set of source file paths.
	/// Returns a hash-based string encoding the combined modification times and paths,
	/// enabling callers to detect whether scripting sources have changed.
	std::string ComputeScriptingLanguageID(const std::vector<std::string>& sourcePaths);

	/// Extension methods on Registry for scripting lifecycle management.
	namespace Scripting {

		/// Subscribe to unload events. Returns a handle for unsubscribing.
		CallbackHandle SubscribeUnload(const UnloadHandler& handler);

		/// Subscribe to reload events. Returns a handle for unsubscribing.
		CallbackHandle SubscribeReload(const ReloadHandler& handler);

		/// Remove a previously subscribed callback by handle.
		void Unsubscribe(CallbackHandle handle);

		/// Unload all reflection classes registered under the given scripting language ID.
		/// Triggers unload callbacks first, then removes matching classes from registry indices.
		void UnloadScriptingReflection(const std::string& scriptingLanguageID);

		/// Invoke reload callbacks for the given scripting language ID.
		void InvokeReloadCallbacks(const std::string& scriptingLanguageID);

		/// Get all classes registered under a scripting language ID.
		const std::vector<const Class*>& GetClassesByScriptingLanguageID(const std::string& scriptingLanguageID);

		/// Check if any classes are registered under the given scripting language ID.
		bool HasScriptingLanguage(const std::string& scriptingLanguageID);
	}

} // namespace Reflect

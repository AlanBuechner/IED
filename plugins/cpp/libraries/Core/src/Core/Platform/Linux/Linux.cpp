#include "Core/Core.h"

#if defined(PLATFORM_LINUX)

#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace Platform
{
	namespace
	{
		constexpr wchar_t c_ReplacementCharacter = 0xFFFD;

		class SpawnActions
		{
		public:
			SpawnActions() { posix_spawn_file_actions_init(&m_Actions); }
			~SpawnActions() { posix_spawn_file_actions_destroy(&m_Actions); }
			DISABLE_COPY_MOVE(SpawnActions)

			void Open(int descriptor, const char* path, int flags) { posix_spawn_file_actions_addopen(&m_Actions, descriptor, path, flags, 0); }
			void Duplicate(int from, int to) { posix_spawn_file_actions_adddup2(&m_Actions, from, to); }
			void Close(int descriptor) { posix_spawn_file_actions_addclose(&m_Actions, descriptor); }
			void ChangeDirectory(const fs::path& dir) { if (!dir.empty()) posix_spawn_file_actions_addchdir_np(&m_Actions, dir.c_str()); }
			const posix_spawn_file_actions_t* Get() const { return &m_Actions; }

		private:
			posix_spawn_file_actions_t m_Actions;
		};

		int WaitForExit(pid_t process)
		{
			int status = 0;
			while (waitpid(process, &status, 0) < 0)
			{
				if (errno != EINTR)
					return -1;
			}
			if (WIFEXITED(status))
				return WEXITSTATUS(status);
			return 128 + WTERMSIG(status);
		}

		std::string ReadAll(int descriptor)
		{
			std::string text;
			char buffer[4096];
			for (;;)
			{
				ssize_t count = read(descriptor, buffer, sizeof(buffer));
				if (count < 0 && errno == EINTR)
					continue;
				if (count <= 0)
					break;
				text.append(buffer, (size_t)count);
			}
			return text;
		}

		uint32 Utf8SequenceLength(unsigned char lead)
		{
			if (lead < 0x80) return 1;
			if ((lead & 0xE0) == 0xC0) return 2;
			if ((lead & 0xF0) == 0xE0) return 3;
			if ((lead & 0xF8) == 0xF0) return 4;
			return 0;
		}

		void AppendUtf8(Core::String& out, uint32 codePoint)
		{
			if (codePoint < 0x80)
				out += (char)codePoint;
			else if (codePoint < 0x800)
			{
				out += (char)(0xC0 | (codePoint >> 6));
				out += (char)(0x80 | (codePoint & 0x3F));
			}
			else if (codePoint < 0x10000)
			{
				out += (char)(0xE0 | (codePoint >> 12));
				out += (char)(0x80 | ((codePoint >> 6) & 0x3F));
				out += (char)(0x80 | (codePoint & 0x3F));
			}
			else
			{
				out += (char)(0xF0 | (codePoint >> 18));
				out += (char)(0x80 | ((codePoint >> 12) & 0x3F));
				out += (char)(0x80 | ((codePoint >> 6) & 0x3F));
				out += (char)(0x80 | (codePoint & 0x3F));
			}
		}
	}

	fs::path GetApplicationPath()
	{
		std::error_code error;
		fs::path path = fs::read_symlink("/proc/self/exe", error);
		return error ? fs::path() : path;
	}

	void* LoadDynamicLibrary(const wchar_t* path)
	{
		void* library = dlopen(WideToUtf8(path).c_str(), RTLD_NOW);
		if (library == nullptr)
			CORE_ERROR("LoadDynamicLibrary: {0}", dlerror());
		return library;
	}

	void* GetLibraryExport(void* h, const char* name)
	{
		void* symbol = dlsym(h, name);
		if (symbol == nullptr)
			CORE_ERROR("GetLibraryExport: {0}", dlerror());
		return symbol;
	}

	void FreeDynamicLibrary(void* h)
	{
		if (h != nullptr)
			dlclose(h);
	}

	int SystemCall(const Core::String& cmd, const fs::path& dir, bool supress)
	{
		SpawnActions actions;
		actions.Open(STDIN_FILENO, "/dev/null", O_RDONLY);
		if (supress)
			actions.Open(STDOUT_FILENO, "/dev/null", O_WRONLY);
		actions.Duplicate(STDOUT_FILENO, STDERR_FILENO);
		actions.ChangeDirectory(dir);
		const char* argv[] = { "/bin/sh", "-c", cmd.c_str(), nullptr };
		pid_t process = 0;
		int error = posix_spawn(&process, "/bin/sh", actions.Get(), nullptr, (char* const*)argv, environ);
		if (error != 0)
			return error;
		return WaitForExit(process);
	}

	std::string ExecuteProcess(const std::string& a_ExePath, const std::vector<std::string>& a_Args)
	{
		int pipeEnds[2];
		if (pipe2(pipeEnds, O_CLOEXEC) != 0)
			return {};
		SpawnActions actions;
		actions.Open(STDIN_FILENO, "/dev/null", O_RDONLY);
		actions.Duplicate(pipeEnds[1], STDOUT_FILENO);
		actions.Duplicate(pipeEnds[1], STDERR_FILENO);
		std::vector<char*> argv;
		argv.push_back((char*)a_ExePath.c_str());
		for (const std::string& arg : a_Args)
			argv.push_back((char*)arg.c_str());
		argv.push_back(nullptr);
		pid_t process = 0;
		int error = posix_spawnp(&process, a_ExePath.c_str(), actions.Get(), nullptr, argv.data(), environ);
		close(pipeEnds[1]);
		if (error != 0)
		{
			close(pipeEnds[0]);
			CORE_WARN("ExecuteProcess: cannot start \"{0}\": {1}", a_ExePath, strerror(error));
			return {};
		}
		std::string output = ReadAll(pipeEnds[0]);
		close(pipeEnds[0]);
		WaitForExit(process);
		return output;
	}

	std::wstring Utf8ToWide(const char* utf8)
	{
		if (utf8 == nullptr)
			return {};
		std::wstring result;
		const unsigned char* at = (const unsigned char*)utf8;
		while (*at != 0)
		{
			uint32 length = Utf8SequenceLength(*at);
			uint32 codePoint = length == 1 ? *at : (length == 0 ? 0 : (*at & (0x7F >> length)));
			uint32 taken = 1;
			while (length > 1 && taken < length && (at[taken] & 0xC0) == 0x80)
				codePoint = (codePoint << 6) | (at[taken++] & 0x3F);
			result += (length == 0 || taken < length) ? c_ReplacementCharacter : (wchar_t)codePoint;
			at += taken;
		}
		return result;
	}

	Core::String WideToUtf8(const wchar_t* wide)
	{
		if (wide == nullptr)
			return {};
		Core::String result;
		for (; *wide != 0; ++wide)
			AppendUtf8(result, (uint32)*wide);
		return result;
	}
}

#endif

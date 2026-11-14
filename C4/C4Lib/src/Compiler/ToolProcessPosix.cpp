#include "Compiler/BuildUtils.h"

#if !defined(_WIN32)

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace Scripting
{
	namespace
	{
		constexpr int kSignalExitBase = 128; // a shell's exit code for a process a signal ended: 128 + the signal

		class SpawnActions
		{
		public:
			SpawnActions() { posix_spawn_file_actions_init(&m_Actions); }
			~SpawnActions() { posix_spawn_file_actions_destroy(&m_Actions); }
			SpawnActions(const SpawnActions&) = delete;
			SpawnActions& operator=(const SpawnActions&) = delete;

			void Duplicate(int from, int to) { posix_spawn_file_actions_adddup2(&m_Actions, from, to); }
			const posix_spawn_file_actions_t* Get() const { return &m_Actions; }

		private:
			posix_spawn_file_actions_t m_Actions;
		};

		struct Pipe
		{
			int reader = -1;
			int writer = -1;

			bool Open()
			{
				int ends[2];
				if (pipe2(ends, O_CLOEXEC) != 0) return false;
				reader = ends[0];
				writer = ends[1];
				return true;
			}
			void CloseWriter() { if (writer >= 0) close(writer); writer = -1; }
			void CloseReader() { if (reader >= 0) close(reader); reader = -1; }
		};

		std::vector<char*> ArgumentVector(const std::vector<std::string>& args)
		{
			std::vector<char*> argv;
			for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
			argv.push_back(nullptr);
			return argv;
		}

		// The process's exit code, or 128 + the signal that ended it; -1 when it cannot be waited for.
		int WaitForExit(pid_t process)
		{
			int status = 0;
			while (waitpid(process, &status, 0) < 0)
				if (errno != EINTR) return -1;
			if (WIFEXITED(status)) return WEXITSTATUS(status);
			return WIFSIGNALED(status) ? kSignalExitBase + WTERMSIG(status) : -1;
		}

		// Reads both pipes until the process closes them, so neither fills while the other is read.
		void ReadBoth(Pipe& out, Pipe& err, std::string& outText, std::string& errText)
		{
			pollfd fds[2] = { { out.reader, POLLIN, 0 }, { err.reader, POLLIN, 0 } };
			std::string* texts[2] = { &outText, &errText };
			int open = 2;
			while (open > 0)
			{
				if (poll(fds, 2, -1) < 0)
				{
					if (errno == EINTR) continue;
					return;
				}
				for (int i = 0; i < 2; ++i)
				{
					if (fds[i].fd < 0 || fds[i].revents == 0) continue;
					char buffer[4096];
					const ssize_t got = read(fds[i].fd, buffer, sizeof(buffer));
					if (got > 0) texts[i]->append(buffer, static_cast<size_t>(got));
					else if (got == 0 || errno != EINTR)
					{
						fds[i].fd = -1;
						--open;
					}
				}
			}
		}
	}

	bool RunTool(const std::vector<std::string>& args)
	{
		if (args.empty()) return false;
		std::vector<char*> argv = ArgumentVector(args);
		pid_t process = 0;
		if (const int error = posix_spawnp(&process, argv[0], nullptr, nullptr, argv.data(), environ); error != 0)
		{
			std::cerr << "error: Failed to start " << args[0] << " (" << std::strerror(error) << ")" << std::endl;
			return false;
		}
		const int exitCode = WaitForExit(process);
		if (exitCode < 0)
		{
			std::cerr << "error: Failed to wait for " << args[0] << std::endl;
			return false;
		}
		if (exitCode != 0)
		{
			std::cerr << "error: Process failed with exit code " << exitCode << std::endl;
			return false;
		}
		return true;
	}

	bool RunToolWithOutput(const Core::Vector<Core::String>& args, int& exitCode,
		std::string& outStdout, std::string& outStderr)
	{
		outStdout.clear();
		outStderr.clear();
		exitCode = -1;
		if (args.Count() == 0) return false;

		Pipe out, err;
		if (!out.Open()) { outStderr = "cannot open a pipe for the tool's output"; return false; }
		if (!err.Open()) { out.CloseReader(); out.CloseWriter(); outStderr = "cannot open a pipe for the tool's errors"; return false; }

		std::vector<std::string> spelled;
		for (size_t i = 0; i < args.Count(); ++i) spelled.push_back(args[i].c_str());
		std::vector<char*> argv = ArgumentVector(spelled);

		SpawnActions actions;
		actions.Duplicate(out.writer, STDOUT_FILENO);
		actions.Duplicate(err.writer, STDERR_FILENO);
		pid_t process = 0;
		const int error = posix_spawnp(&process, argv[0], actions.Get(), nullptr, argv.data(), environ);
		out.CloseWriter();
		err.CloseWriter();
		if (error != 0)
		{
			out.CloseReader();
			err.CloseReader();
			outStderr = "cannot start " + spelled[0] + " (" + std::strerror(error) + ")";
			return false;
		}
		ReadBoth(out, err, outStdout, outStderr);
		out.CloseReader();
		err.CloseReader();
		exitCode = WaitForExit(process);
		return true;
	}
} // namespace Scripting

#endif

#include "ServeChannel.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace
{
	constexpr unsigned c_MillisecondsPerSecond = 1000;
	constexpr size_t c_ReadChunk = 4096;

	// The build's output, kept in a file of its own while it runs and sent to the client after: the tools a build starts
	// write to the standard handles they inherit, and a file takes every writer as it comes.
	std::filesystem::path OutputPath()
	{
		std::error_code ec;
		const std::filesystem::path folder = std::filesystem::temp_directory_path(ec);
#ifdef _WIN32
		const unsigned long process = GetCurrentProcessId();
#else
		const unsigned long process = static_cast<unsigned long>(getpid());
#endif
		return (ec ? std::filesystem::path(".") : folder) / ("c4-serve-" + std::to_string(process) + ".out");
	}

	std::string ReadAll(const std::filesystem::path& file)
	{
		std::ifstream in(file, std::ios::binary);
		std::ostringstream text;
		text << in.rdbuf();
		return text.str();
	}

	// Standard output and error pointed at a file while alive, the process's and the tools' it starts.
	class OutputRedirect final
	{
	public:
		explicit OutputRedirect(const std::filesystem::path& path)
		{
			std::cout.flush();
			std::cerr.flush();
			std::fflush(nullptr);
#ifdef _WIN32
			m_SavedOut = _dup(1);
			m_SavedErr = _dup(2);
			m_OldOut = GetStdHandle(STD_OUTPUT_HANDLE);
			m_OldErr = GetStdHandle(STD_ERROR_HANDLE);
			const int fd = _wopen(path.c_str(), _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY, 0600);
			if (fd < 0) return;
			_dup2(fd, 1);
			_dup2(fd, 2);
			_close(fd);
			const HANDLE file = reinterpret_cast<HANDLE>(_get_osfhandle(1));
			SetHandleInformation(file, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
			SetStdHandle(STD_OUTPUT_HANDLE, file);
			SetStdHandle(STD_ERROR_HANDLE, file);
#else
			m_SavedOut = dup(1);
			m_SavedErr = dup(2);
			const int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
			if (fd < 0) return;
			dup2(fd, 1);
			dup2(fd, 2);
			close(fd);
#endif
		}

		~OutputRedirect()
		{
			std::cout.flush();
			std::cerr.flush();
			std::fflush(nullptr);
#ifdef _WIN32
			_dup2(m_SavedOut, 1);
			_dup2(m_SavedErr, 2);
			_close(m_SavedOut);
			_close(m_SavedErr);
			SetStdHandle(STD_OUTPUT_HANDLE, m_OldOut);
			SetStdHandle(STD_ERROR_HANDLE, m_OldErr);
#else
			dup2(m_SavedOut, 1);
			dup2(m_SavedErr, 2);
			close(m_SavedOut);
			close(m_SavedErr);
#endif
		}

		OutputRedirect(const OutputRedirect&) = delete;
		OutputRedirect& operator=(const OutputRedirect&) = delete;

	private:
		int m_SavedOut = -1;
		int m_SavedErr = -1;
#ifdef _WIN32
		HANDLE m_OldOut = nullptr;
		HANDLE m_OldErr = nullptr;
#endif
	};

	// What every channel does with the build's output and the lines it reads.
	class LineChannel : public ServeChannel
	{
	public:
		std::optional<std::string> ReadLine() override
		{
			while (true)
			{
				if (const size_t end = m_Pending.find('\n'); end != std::string::npos)
				{
					std::string line = m_Pending.substr(0, end);
					m_Pending.erase(0, end + 1);
					if (!line.empty() && line.back() == '\r') line.pop_back();
					return line;
				}
				char chunk[c_ReadChunk];
				const long read = Read(chunk, sizeof(chunk));
				if (read <= 0) return std::nullopt;
				m_Pending.append(chunk, static_cast<size_t>(read));
			}
		}

		void WriteLine(const std::string& line) override { WriteAll(line + "\n"); }

	protected:
		virtual long Read(char* into, size_t size) = 0;
		virtual void WriteAll(const std::string& text) = 0;

		void RedirectOutput() override { m_Output = std::make_unique<OutputRedirect>(m_OutputPath); }

		void RestoreOutput() override
		{
			m_Output.reset();
			const std::string text = ReadAll(m_OutputPath);
			std::error_code ec;
			std::filesystem::remove(m_OutputPath, ec);
			if (!text.empty()) WriteAll(text);
		}

		std::string m_Pending;
		const std::filesystem::path m_OutputPath = OutputPath();
		std::unique_ptr<OutputRedirect> m_Output;
	};

#ifdef _WIN32
	// A named pipe `\\.\pipe\<name>`, its first and only instance: a second server of the name is refused.
	class PipeChannel final : public LineChannel
	{
	public:
		static std::unique_ptr<ServeChannel> Listen(const std::string& name, std::string& error)
		{
			const std::string path = "\\\\.\\pipe\\" + name;
			const HANDLE pipe = CreateNamedPipeA(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1 << 16, 1 << 16, 0, nullptr);
			if (pipe == INVALID_HANDLE_VALUE)
			{
				const DWORD code = GetLastError();
				if (code != ERROR_ACCESS_DENIED && code != ERROR_PIPE_BUSY) error = "cannot listen on " + path + " (Win32 error " + std::to_string(code) + ")";
				return nullptr;
			}
			return std::unique_ptr<ServeChannel>(new PipeChannel(pipe));
		}

		~PipeChannel() override
		{
			CloseHandle(m_Event);
			CloseHandle(m_Pipe);
		}

		bool Accept(unsigned idleSeconds) override
		{
			OVERLAPPED pending = Pending();
			if (ConnectNamedPipe(m_Pipe, &pending)) return true;
			const DWORD code = GetLastError();
			if (code == ERROR_PIPE_CONNECTED) return true;
			if (code != ERROR_IO_PENDING) return false;
			if (WaitForSingleObject(m_Event, idleSeconds * c_MillisecondsPerSecond) == WAIT_OBJECT_0) return true;
			CancelIo(m_Pipe);
			DWORD ignored = 0;
			GetOverlappedResult(m_Pipe, &pending, &ignored, TRUE);
			return false;
		}

		void Hangup() override
		{
			FlushFileBuffers(m_Pipe);
			DisconnectNamedPipe(m_Pipe);
			m_Pending.clear();
		}

	protected:
		long Read(char* into, size_t size) override
		{
			OVERLAPPED pending = Pending();
			DWORD read = 0;
			if (!ReadFile(m_Pipe, into, static_cast<DWORD>(size), &read, &pending) && GetLastError() != ERROR_IO_PENDING) return -1;
			if (!GetOverlappedResult(m_Pipe, &pending, &read, TRUE)) return -1;
			return static_cast<long>(read);
		}

		void WriteAll(const std::string& text) override
		{
			size_t at = 0;
			while (at < text.size())
			{
				OVERLAPPED pending = Pending();
				DWORD written = 0;
				const DWORD size = static_cast<DWORD>(text.size() - at);
				if (!WriteFile(m_Pipe, text.data() + at, size, &written, &pending) && GetLastError() != ERROR_IO_PENDING) return;
				if (!GetOverlappedResult(m_Pipe, &pending, &written, TRUE) || written == 0) return;
				at += written;
			}
		}

	private:
		explicit PipeChannel(HANDLE pipe) : m_Pipe(pipe), m_Event(CreateEventA(nullptr, TRUE, FALSE, nullptr)) {}

		OVERLAPPED Pending()
		{
			OVERLAPPED pending{};
			ResetEvent(m_Event);
			pending.hEvent = m_Event;
			return pending;
		}

		HANDLE m_Pipe;
		HANDLE m_Event;
	};
#else
	// A Unix socket `<temp>/<name>.sock`; a socket of the name another server answers on is left to it.
	class SocketChannel final : public LineChannel
	{
	public:
		static std::unique_ptr<ServeChannel> Listen(const std::string& name, std::string& error)
		{
			const std::string path = (std::filesystem::temp_directory_path() / (name + ".sock")).string();
			sockaddr_un where{};
			where.sun_family = AF_UNIX;
			if (path.size() >= sizeof(where.sun_path))
			{
				error = "the socket path " + path + " is too long";
				return nullptr;
			}
			std::strncpy(where.sun_path, path.c_str(), sizeof(where.sun_path) - 1);
			const int listening = socket(AF_UNIX, SOCK_STREAM, 0);
			if (listening < 0) return nullptr;
			if (bind(listening, reinterpret_cast<sockaddr*>(&where), sizeof(where)) != 0)
			{
				// A socket left by a server that ended is taken over; one a server answers on is its.
				const int probe = socket(AF_UNIX, SOCK_STREAM, 0);
				const bool answered = probe >= 0 && connect(probe, reinterpret_cast<sockaddr*>(&where), sizeof(where)) == 0;
				if (probe >= 0) close(probe);
				if (answered || unlink(path.c_str()) != 0 || bind(listening, reinterpret_cast<sockaddr*>(&where), sizeof(where)) != 0)
				{
					close(listening);
					return nullptr;
				}
			}
			if (listen(listening, 1) != 0)
			{
				error = "cannot listen on " + path + ": " + std::strerror(errno);
				close(listening);
				return nullptr;
			}
			return std::unique_ptr<ServeChannel>(new SocketChannel(listening, path));
		}

		~SocketChannel() override
		{
			Hangup();
			close(m_Listening);
			unlink(m_Path.c_str());
		}

		bool Accept(unsigned idleSeconds) override
		{
			pollfd waiting{ m_Listening, POLLIN, 0 };
			if (poll(&waiting, 1, static_cast<int>(idleSeconds * c_MillisecondsPerSecond)) <= 0) return false;
			m_Client = accept(m_Listening, nullptr, nullptr);
			return m_Client >= 0;
		}

		void Hangup() override
		{
			if (m_Client >= 0) close(m_Client);
			m_Client = -1;
			m_Pending.clear();
		}

	protected:
		long Read(char* into, size_t size) override { return static_cast<long>(recv(m_Client, into, size, 0)); }

		void WriteAll(const std::string& text) override
		{
			size_t at = 0;
			while (at < text.size())
			{
				const ssize_t sent = send(m_Client, text.data() + at, text.size() - at, MSG_NOSIGNAL);
				if (sent <= 0) return;
				at += static_cast<size_t>(sent);
			}
		}

	private:
		SocketChannel(int listening, std::string path) : m_Listening(listening), m_Path(std::move(path)) {}

		int m_Listening;
		int m_Client = -1;
		std::string m_Path;
	};
#endif
}

std::unique_ptr<ServeChannel> ServeChannel::Listen(const std::string& name, std::string& error)
{
#ifdef _WIN32
	return PipeChannel::Listen(name, error);
#else
	return SocketChannel::Listen(name, error);
#endif
}

ServeChannel::OutputToClient::OutputToClient(ServeChannel& channel) : m_Channel(channel)
{
	m_Channel.RedirectOutput();
}

ServeChannel::OutputToClient::~OutputToClient()
{
	m_Channel.RestoreOutput();
}

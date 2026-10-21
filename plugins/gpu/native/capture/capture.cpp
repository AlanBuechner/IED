#include "capture.h"
#include "control.h"

#include <windows.h>

namespace fb::capture
{
	Session& Session::Get() {
		// The library's one session: made the first time a hook or an API call needs it, ends with the process.
		static Session* session = new Session();
		return *session;
	}

	Session::Session() {
		pipe_ = std::make_unique<ControlPipe>(*this);
		pipe_->Start();
	}

	Session::~Session() = default;

	std::wstring Session::CaptureDir() {
		std::lock_guard<std::mutex> guard(lock_);
		if (!dir_.empty()) return dir_;
		wchar_t value[MAX_PATH * 2];
		const DWORD length = GetEnvironmentVariableW(L"FLASHBANG_CAPTURE_DIR", value, MAX_PATH * 2);
		if (length > 0 && length < MAX_PATH * 2) dir_ = value;
		else {
			wchar_t temp[MAX_PATH];
			GetTempPathW(MAX_PATH, temp);
			dir_ = std::wstring(temp) + L"flashbang";
		}
		return dir_;
	}

	void Session::SetCaptureDir(const std::string& folder) {
		std::lock_guard<std::mutex> guard(lock_);
		dir_ = Widen(folder);
	}

	void Session::Trigger(int frames) {
		if (frames < 1) frames = 1;
		pending_ += frames;
		Log("capture of %d frame(s) requested", frames);
	}

	bool Session::TakeTrigger() {
		int pending = pending_.load();
		while (pending > 0) {
			if (pending_.compare_exchange_weak(pending, pending - 1)) return true;
		}
		return false;
	}

	void Session::SetComment(const std::string& text) {
		std::lock_guard<std::mutex> guard(lock_);
		comment_ = text;
	}

	std::string Session::Comment() {
		std::lock_guard<std::mutex> guard(lock_);
		return comment_;
	}

	void Session::Annotate(const std::string& key, const std::string& value) {
		std::lock_guard<std::mutex> guard(lock_);
		for (auto& annotation : annotations_) {
			if (annotation.first == key) {
				annotation.second = value;
				return;
			}
		}
		annotations_.emplace_back(key, value);
	}

	std::vector<std::pair<std::string, std::string>> Session::Annotations() {
		std::lock_guard<std::mutex> guard(lock_);
		return annotations_;
	}

	static void MakeFolders(const std::wstring& folder) {
		for (size_t i = 3; i <= folder.size(); ++i) {
			if (i == folder.size() || folder[i] == L'\\' || folder[i] == L'/') CreateDirectoryW(folder.substr(0, i).c_str(), nullptr);
		}
	}

	std::wstring Session::NewCaptureBase(u64 frame) {
		std::wstring dir = CaptureDir();
		MakeFolders(dir);
		std::string name = Format("%s_%s_f%llu", ExecutableName().c_str(), TimeStamp().c_str(), (unsigned long long)frame);
		std::wstring base = dir + L"\\" + Widen(name);
		// Two captures within a second of the same frame number get a suffix.
		std::wstring candidate = base;
		for (int n = 2; GetFileAttributesW((candidate + L".flash").c_str()) != INVALID_FILE_ATTRIBUTES; ++n)
			candidate = base + L"_" + std::to_wstring(n);
		return candidate;
	}

	static std::string FileName(const std::wstring& path) {
		size_t slash = path.find_last_of(L"\\/");
		return Narrow(slash == std::wstring::npos ? path : path.substr(slash + 1));
	}

	void Session::Written(const std::wstring& base, u64 bytes, const CaptureSummary& summary) {
		const std::string path = Narrow(base + L".flash");
		JsonWriter json;
		json.BeginObject();
		json.Uint("version", 1);
		json.String("file", FileName(base + L".flash"));
		if (summary.thumbnail) json.String("thumbnail", FileName(base + L".png"));
		json.String("program", ExecutableName());
		json.String("programPath", Narrow(ExecutablePath()));
		json.Uint("pid", GetCurrentProcessId());
		json.String("api", summary.api);
		json.Uint("frame", summary.frame);
		json.String("time", TimeStamp());
		json.String("gpu", summary.gpu);
		json.String("driver", summary.driver);
		json.Uint("width", summary.width);
		json.Uint("height", summary.height);
		json.Uint("bytes", bytes);
		json.Uint("events", summary.events);
		json.String("comment", Comment());
		json.BeginArray("warnings");
		for (const std::string& warning : summary.warnings) json.String(warning);
		json.EndArray();
		json.BeginObject("annotations");
		for (const auto& annotation : Annotations()) json.String(annotation.first.c_str(), annotation.second);
		json.EndObject();
		json.EndObject();
		const std::string text = json.Take();
		HANDLE file = CreateFileW((base + L".json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			DWORD wrote = 0;
			WriteFile(file, text.data(), (DWORD)text.size(), &wrote, nullptr);
			CloseHandle(file);
		}
		{
			std::lock_guard<std::mutex> guard(lock_);
			captures_.push_back({path, summary.frame, bytes});
		}
		Log("captured frame %llu: %s (%llu bytes)", (unsigned long long)summary.frame, path.c_str(), (unsigned long long)bytes);
		JsonWriter event;
		event.BeginObject().String("event", "captured").String("path", path).Uint("frame", summary.frame).Uint("bytes", bytes).EndObject();
		Post(event.Take());
	}

	void Session::Failed(const std::string& why) {
		Log("capture failed: %s", why.c_str());
		JsonWriter event;
		event.BeginObject().String("event", "error").String("message", why).EndObject();
		Post(event.Take());
	}

	std::vector<WrittenCapture> Session::Captures() {
		std::lock_guard<std::mutex> guard(lock_);
		return captures_;
	}

	void Session::NoteApi(const char* api) {
		{
			std::lock_guard<std::mutex> guard(lock_);
			for (const std::string& known : apis_)
				if (known == api) return;
			apis_.push_back(api);
		}
		Post(HelloJson());
	}

	void Session::Post(const std::string& jsonLine) {
		if (pipe_) pipe_->Send(jsonLine);
	}

	std::string Session::HelloJson() {
		JsonWriter json;
		json.BeginObject();
		json.String("event", "hello");
		json.Uint("pid", GetCurrentProcessId());
		json.String("program", ExecutableName());
		json.BeginArray("apis");
		{
			std::lock_guard<std::mutex> guard(lock_);
			for (const std::string& api : apis_) json.String(api);
		}
		json.EndArray();
		json.Uint("frame", frame_.load());
		json.String("captureDir", Narrow(CaptureDir()));
		json.Bool("capturing", capturing_.load());
		json.Int("pending", pending_.load());
		json.EndObject();
		return json.Take();
	}
}

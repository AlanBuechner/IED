// capture.h - the capture library's state in the program (docs/22 "Capture"): what is asked for (a frame now, the next
// N frames, manual frame bounds), where captures go, the comment and annotations to put in them, the captures written,
// and the control pipe to IED. One Session per loaded library, made when it loads; the API layers (D3D12 wrappers,
// the Vulkan layer) ask it at each frame boundary whether to capture.
#pragma once

#include "fb_base.h"
#include "json.h"

#include <atomic>
#include <mutex>

namespace fb::capture
{
	class ControlPipe;

	struct WrittenCapture
	{
		std::string path;
		u64 frame;
		u64 bytes;
	};

	// What a finished capture says about itself: the sidecar JSON beside the file and the "captured" event.
	struct CaptureSummary
	{
		std::string api;
		std::string gpu;
		std::string driver;
		u64 frame = 0;
		u32 width = 0;
		u32 height = 0;
		bool thumbnail = false;   // the presented image was saved beside the capture
		u64 events = 0;
		std::vector<std::string> warnings;
	};

	class Session
	{
	public:
		static Session& Get();

		Session();
		~Session();

		// The folder captures go to: FLASHBANG_CAPTURE_DIR, else SetCaptureDir, else %TEMP%\flashbang.
		std::wstring CaptureDir();
		void SetCaptureDir(const std::string& folder);

		// Capture the next `frames` frames (each its own file).
		void Trigger(int frames);
		// At a frame boundary: true (and one less pending) when a capture should start now.
		bool TakeTrigger();
		bool Pending() const { return pending_.load() > 0; }


		void SetComment(const std::string& text);
		std::string Comment();
		void Annotate(const std::string& key, const std::string& value);
		std::vector<std::pair<std::string, std::string>> Annotations();

		// A new capture's path (without extension): <dir>/<program>_<time>_f<frame>.
		std::wstring NewCaptureBase(u64 frame);
		// Records a written capture, writes its sidecar and tells IED.
		void Written(const std::wstring& base, u64 bytes, const CaptureSummary& summary);
		void Failed(const std::string& why);
		std::vector<WrittenCapture> Captures();
		bool Capturing() const { return capturing_.load(); }
		void SetCapturing(bool on) { capturing_ = on; }

		u64 NextFrame() { return ++frame_; }
		u64 Frame() const { return frame_.load(); }
		void NoteApi(const char* api);

		// A line to IED over the control pipe (when connected).
		void Post(const std::string& jsonLine);
		// The control pipe's hello and status events.
		std::string HelloJson();

	private:
		std::mutex lock_;
		std::atomic<int> pending_{0};
		std::atomic<bool> capturing_{false};
		std::atomic<u64> frame_{0};
		std::wstring dir_;
		std::string comment_;
		std::vector<std::pair<std::string, std::string>> annotations_;
		std::vector<WrittenCapture> captures_;
		std::vector<std::string> apis_;
		std::unique_ptr<ControlPipe> pipe_;
	};
}

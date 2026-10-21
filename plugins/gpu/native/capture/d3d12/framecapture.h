// framecapture.h - one wrapped D3D12 device's frame capture (docs/22 "Capture"): at the frame boundary after a trigger it
// waits for the GPU, writes every live object's creation record, the descriptor heaps' contents, the resources' states
// and their initial contents (GPU copies into readback memory, or the mapped memory of upload heaps), then records the
// frame - submissions with their lists' commands, CPU writes to mapped memory, fences, markers, objects made - until
// the next Present, and finishes the file with the frame, a thumbnail and the metadata.
#pragma once

#include "capfile.h"
#include "capture/capture.h"
#include "tracker.h"

#include <mutex>

namespace fb::capture::d3d12
{
	class WrappedDevice;
	class WrappedQueue;
	class WrappedList;
	class WrappedSwapChain;

	class FrameCapture
	{
	public:
		explicit FrameCapture(WrappedDevice& device) : device_(device) {}
		~FrameCapture();

		bool Active() const { return active_; }

		// Frame boundaries: a Present (before it shows the frame, and after), or the program's own bounds.
		void BeforePresent(WrappedSwapChain* swapChain, UINT syncInterval, UINT flags);
		void AfterPresent();
		void ManualStart();
		void ManualEnd();

		// What happens while a frame is captured.
		void BeforeExecute(WrappedQueue* queue, const std::vector<WrappedList*>& lists);
		void QueueFence(u32 recordType, WrappedQueue* queue, ID3D12Fence* fence, u64 value);
		void QueueMarker(WrappedQueue* queue, MarkerOp op, UINT metadata, const void* data, UINT size);
		void Created(Object* object);
		void DescriptorWritten(const DescriptorWrite& view);
		void DescriptorsCopied(const std::vector<DescriptorCopy>& runs);
		void Warn(const std::string& text);

	private:
		bool Begin();
		void End(WrappedSwapChain* swapChain, UINT syncInterval, UINT flags);
		bool MakeObjects();
		void WaitIdle();
		void Execute();
		void WriteSetup();
		void WriteInitialContents();
		void ReadUploads(const std::vector<Object*>& live);
		void CopyResources(const std::vector<Object*>& live);
		void DiffUploads();
		void Thumbnail(WrappedSwapChain* swapChain, CaptureSummary& summary);
		std::string MetaJson(const CaptureSummary& summary);

		template <class R> void Record(u32 type, R& record) {
			ByteWriter payload;
			Ser s(payload, MapForRecords());
			Serialize(s, record);
			RecordHeader header = {type, 0, payload.Size()};
			stream_.Put(header);
			stream_.Bytes(payload.Data(), payload.Size());
		}
		template <class R> void FileRecord(u32 type, R& record) {
			ByteWriter payload;
			Ser s(payload, MapForRecords());
			Serialize(s, record);
			file_.Record(type, payload);
		}
		SerWriteMap* MapForRecords();

		WrappedDevice& device_;
		std::recursive_mutex lock_;
		bool active_ = false;
		u64 frame_ = 0;
		CaptureWriter file_;
		std::wstring base_;
		ByteWriter stream_;
		u64 events_ = 0;
		std::vector<std::string> warnings_;

		ID3D12CommandQueue* queue_ = nullptr;
		ID3D12CommandAllocator* allocator_ = nullptr;
		ID3D12GraphicsCommandList* list_ = nullptr;
		ID3D12Fence* fence_ = nullptr;
		u64 fenceValue_ = 0;
		void* event_ = nullptr;

		struct Shadow
		{
			u64 id;
			ID3D12Resource* resource;
			std::vector<u8> bytes;
		};
		std::vector<Shadow> shadows_;
	};
}

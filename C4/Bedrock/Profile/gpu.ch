// GPU zones for the IED profiler (IED docs/16 "GPU timing"): a pass's time on a GPU queue beside the CPU zones. Bedrock
// records no GPU commands: the program owns the timestamp query heap, writes the two timestamps of each zone into the
// slots BeginZone hands out, reads them back once the GPU is done and gives them to Time. Its renderer also emits the
// API's own markers (PIX events), named after the zone's site.
//     Profile::GpuContext gpu(Profile::GpuApi::D3D12, "Direct queue", periodPicoseconds, 256);
//     {
//         Profile::GpuZone zone(gpu, s_shadowPass);                     // GpuZoneBegin now, on this thread
//         list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, zone.BeginQuery());
//         ... the pass ...
//         list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, zone.EndQuery());
//     }                                                                 // GpuZoneEnd
//     ... after the frame's fence: gpu.Time(slot, ticks) for each slot written, and now and then
//     gpu.Calibrate(cpu, gpu) from ID3D12CommandQueue::GetClockCalibration.

include "profile.ch"

namespace Profile
{
	// The API a context's queue belongs to, as GpuContextDef numbers it.
	enum class GpuApi : uint64 { Other = 0, D3D12 = 1, Vulkan = 2, OpenGL = 3, Metal = 4 }

	// One GPU queue. Each zone takes two query slots, its begin and its end, round a ring of `queryCount` (made even, at
	// least 2), so a slot is in use again `queryCount / 2` zones later: the ring must hold every zone not yet read back.
	// Threads may begin zones on one context at once (parallel command recording): the slots come from an atomic
	// counter, and each thread's zones nest on their own. Without a session nothing is sent; a zone then costs the
	// counter's atomic add.
	class GpuContext
	{
		// `periodPicoseconds`: one GPU tick (D3D12: 10^12 / GetTimestampFrequency).
		[operator("create")] public void Init(GpuApi api, Core::StringView queue, uint64 periodPicoseconds, uint32 queryCount);

		// A pair of readings of the CPU's and the GPU's clocks taken together (D3D12 GetClockCalibration): `cpuTicks`
		// on the system's counter (Platform::Ticks, QueryPerformanceCounter). Send one at the start and now and then.
		public void Calibrate(int64 cpuTicks, uint64 gpuTicks);
		// Begins a zone at `site` now, on the calling thread: the slot for its start timestamp. Its end's is the next.
		public uint32 BeginZone(Site& site);
		// Ends the calling thread's innermost zone, which BeginZone gave `query`.
		public void EndZone(uint32 query);
		// The GPU timestamp read back from slot `query`.
		public void Time(uint32 query, uint64 gpuTicks);

		public uint32 QueryCount() const { return queryCount; }

		public GpuApi api;
		public Core::String queue;
		public uint64 periodPicoseconds;
		public uint64 id;                 // its id in the session `session`
		public int64 session;
		private uint32 queryCount;
		private int64 zones;              // atomic: the zones begun so far
	}

	// A GPU zone for a scope of recording code: BeginZone at its creation, EndZone at its end (when the session it began
	// in still runs).
	struct GpuZone
	{
		[operator("create")] public void Init(GpuContext& context, Site& site);
		[operator("delete")] public void Delete();

		public uint32 BeginQuery() const { return begin; }
		public uint32 EndQuery() const { return begin + 1; }

		private GpuContext* context;
		private uint32 begin;
		private int64 session;
	}
}

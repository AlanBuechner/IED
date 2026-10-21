// The client's example, and the program test_itp_rust.py runs: a game-like loop with zones on two threads, a zone
// named per call with text and a value, frames, plots, a message, tasks, a pool and every allocation of the global
// allocator. The target is the first argument ("file:<path>" or "host:port"); without one, IED_PROFILER.

#[global_allocator]
static ALLOCATOR: ied_profiler::Tracked<std::alloc::System> = ied_profiler::Tracked(std::alloc::System);

fn main() {
    if let Some(target) = std::env::args().nth(1) {
        if let Err(problem) = ied_profiler::start(&target, "itp-rust-test") {
            println!("start failed: {problem}");
            std::process::exit(2);
        }
    }
    ied_profiler::frame_begin("Loading");
    spin(1000);
    ied_profiler::frame_end("Loading");
    for frame in 0..5 {
        update(frame);
        ied_profiler::frame();
    }
    ied_profiler::set_thread_name("Main loop done");
    ied_profiler::message(ied_profiler::Severity::Warning, "done");
    let block = [0u8; 64];
    ied_profiler::pool_name(2, "Arena");
    ied_profiler::alloc(block.as_ptr() as usize, block.len(), 2);
    ied_profiler::free(block.as_ptr() as usize, 2);
}

fn update(frame: u32) {
    let _zone = ied_profiler::zone!("Update");
    {
        let load = ied_profiler::zone!("Load", "level.bin");
        load.text("bytes=4096");
        load.value(4096);
        let names: Vec<String> = (0..16).map(|_| "x".repeat(64)).collect();
        spin(names.len() as u64 * 1000);
    }
    std::thread::Builder::new()
        .name("Worker".to_string())
        .spawn(|| {
            let _task = ied_profiler::Task::begin("Physics", None);
            let _zone = ied_profiler::zone!("Physics::Step");
            spin(20000);
        })
        .unwrap()
        .join()
        .unwrap();
    ied_profiler::plot("Frame", frame as f64);
}

fn spin(count: u64) -> u64 {
    let _zone = ied_profiler::zone!("Spin");
    (0..count).fold(0, |sum, i| std::hint::black_box(sum + i))
}

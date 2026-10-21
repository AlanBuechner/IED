// The `tracing` feature's example, and test_itp_rust.py's second program: spans become zones (their fields the
// zone's text), events become messages.

fn main() {
    if let Some(target) = std::env::args().nth(1) {
        if let Err(problem) = ied_profiler::start(&target, "itp-rust-spans") {
            println!("start failed: {problem}");
            std::process::exit(2);
        }
    }
    tracing::subscriber::set_global_default(ied_profiler::tracing::Subscriber::new()).unwrap();
    for frame in 0..3u32 {
        let span = tracing::info_span!("frame", number = frame);
        let _entered = span.enter();
        load("level.bin");
        tracing::warn!(frame, "slow frame");
    }
}

fn load(path: &str) {
    let span = tracing::debug_span!("load", path);
    let _entered = span.enter();
    tracing::info!("loaded");
}

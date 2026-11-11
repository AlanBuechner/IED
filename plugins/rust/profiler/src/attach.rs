// IED's Attach to Process (IED docs/16 "Attaching to a running program"), as Bedrock's Profile/attach.cf and the C/C++
// client's Itp.cpp: a program started without IED_PROFILER listens on the local pipe ied-itp-<pid>, one line each way -
// hello ("itp-attach 1 <idle|attached> <app>"), attach <target> ("ok", "busy", "error <why>"; a target on this machine;
// the programs it starts after inherit it), detach ("ok"). Windows only: elsewhere there is nothing to attach to.

#[cfg(windows)]
mod imp {
    use crate::session::{is_on, start_session, stop_session};
    use std::ffi::c_void;
    use std::sync::atomic::{AtomicBool, Ordering};

    type Handle = *mut c_void;
    const INVALID_HANDLE_VALUE: Handle = -1isize as Handle;
    const PIPE_ACCESS_DUPLEX: u32 = 0x3;
    const PIPE_TYPE_BYTE_WAIT_REJECT_REMOTE: u32 = 0x8;
    const PIPE_UNLIMITED_INSTANCES: u32 = 255;
    const ERROR_PIPE_CONNECTED: u32 = 535;

    #[link(name = "kernel32")]
    extern "system" {
        fn CreateNamedPipeW(name: *const u16, open_mode: u32, pipe_mode: u32, instances: u32, out_size: u32, in_size: u32, timeout: u32, security: *mut c_void) -> Handle;
        fn ConnectNamedPipe(pipe: Handle, overlapped: *mut c_void) -> i32;
        fn DisconnectNamedPipe(pipe: Handle) -> i32;
        fn ReadFile(file: Handle, buffer: *mut u8, to_read: u32, read: *mut u32, overlapped: *mut c_void) -> i32;
        fn WriteFile(file: Handle, buffer: *const u8, to_write: u32, written: *mut u32, overlapped: *mut c_void) -> i32;
        fn FlushFileBuffers(file: Handle) -> i32;
        fn CloseHandle(handle: Handle) -> i32;
        fn GetLastError() -> u32;
    }

    static LISTENING: AtomicBool = AtomicBool::new(false);

    fn is_local_target(target: &str) -> bool {
        target.starts_with("127.0.0.1:") || target.starts_with("localhost:") || target.starts_with("[::1]:")
    }

    fn answer(line: &str, app: &str) -> String {
        if line == "hello" {
            return format!("itp-attach 1 {} {app}", if is_on() { "attached" } else { "idle" });
        }
        if let Some(target) = line.strip_prefix("attach ") {
            let target = target.trim();
            if !is_local_target(target) {
                return "error the target is not on this machine".to_string();
            }
            if is_on() {
                return "busy".to_string();
            }
            return match start_session(target, app) {
                Ok(()) => {
                    std::env::set_var("IED_PROFILER", target);
                    "ok".to_string()
                }
                Err(problem) => format!("error {problem}"),
            };
        }
        if line == "detach" {
            stop_session();
            std::env::remove_var("IED_PROFILER");
            return "ok".to_string();
        }
        "error not a request: hello, attach <target> or detach".to_string()
    }

    fn serve(app: String) {
        let name: Vec<u16> = format!("\\\\.\\pipe\\ied-itp-{}", std::process::id()).encode_utf16().chain(std::iter::once(0)).collect();
        loop {
            // SAFETY: the name is a terminated UTF-16 string that outlives the call; the handle is closed below.
            let pipe = unsafe { CreateNamedPipeW(name.as_ptr(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE_WAIT_REJECT_REMOTE, PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, std::ptr::null_mut()) };
            if pipe == INVALID_HANDLE_VALUE {
                return;
            }
            // SAFETY: a valid pipe handle, synchronous calls with buffers that outlive them.
            unsafe {
                if ConnectNamedPipe(pipe, std::ptr::null_mut()) == 0 && GetLastError() != ERROR_PIPE_CONNECTED {
                    CloseHandle(pipe);
                    continue;
                }
                let mut line = Vec::new();
                let mut buffer = [0u8; 256];
                while line.len() < 4096 {
                    let mut got = 0u32;
                    if ReadFile(pipe, buffer.as_mut_ptr(), buffer.len() as u32, &mut got, std::ptr::null_mut()) == 0 || got == 0 {
                        break;
                    }
                    line.extend_from_slice(&buffer[..got as usize]);
                    if line.contains(&b'\n') {
                        break;
                    }
                }
                let text = String::from_utf8_lossy(&line);
                let request = text.split('\n').next().unwrap_or("").trim();
                if !request.is_empty() {
                    let reply = answer(request, &app) + "\n";
                    let mut wrote = 0u32;
                    WriteFile(pipe, reply.as_ptr(), reply.len() as u32, &mut wrote, std::ptr::null_mut());
                    FlushFileBuffers(pipe);
                }
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
            }
        }
    }

    /// Starts the attach pipe's listener, once.
    pub fn listen_for_attach(app: String) {
        if LISTENING.swap(true, Ordering::AcqRel) {
            return;
        }
        std::thread::spawn(move || serve(app));
    }
}

#[cfg(windows)]
pub use imp::listen_for_attach;

#[cfg(not(windows))]
pub fn listen_for_attach(_app: String) {}

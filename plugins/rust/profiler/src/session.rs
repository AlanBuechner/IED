// The session: one per process, started from IED_PROFILER on the first call or by `start`. Each thread writes its
// events into a buffer of its own; a flusher thread sends them every 10 ms, the global queue's chunk first so a
// definition always reaches the profiler before its first use.

use crate::encode::*;
use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::io::Write;
use std::net::{TcpStream, ToSocketAddrs};
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicPtr, AtomicU64, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};
use std::thread::JoinHandle;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

const FLUSH_EVERY: Duration = Duration::from_millis(10);
const CALIBRATE_EVERY_NS: i64 = 1_000_000_000;
const CONNECT_TIMEOUT: Duration = Duration::from_secs(5);
const TICKS_PER_SECOND: u64 = 1_000_000_000;

const UNKNOWN: u8 = 0;
const OFF: u8 = 1;
const ON: u8 = 2;

static STATE: AtomicU8 = AtomicU8::new(UNKNOWN);
static CURRENT: AtomicPtr<Session> = AtomicPtr::new(std::ptr::null_mut());
static STARTING: Mutex<i64> = Mutex::new(0);
static EPOCH: OnceLock<Instant> = OnceLock::new();

/// The client's clock: nanoseconds of the monotonic clock since the process first used it.
pub fn now() -> i64 {
    EPOCH.get_or_init(Instant::now).elapsed().as_nanos() as i64
}

fn unix_ns() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_nanos() as u64).unwrap_or(0)
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

// --- Inside the client -----------------------------------------------------------------------------------------

thread_local! {
    // Set while the calling thread runs the client's own code (and for the flusher's whole life): an allocation made
    // there is not reported, so a global allocator that reports to `alloc` neither recurses nor deadlocks.
    static INSIDE: Cell<u32> = const { Cell::new(0) };
}

pub struct Inside;

impl Inside {
    pub fn enter() -> Inside {
        INSIDE.with(|inside| inside.set(inside.get() + 1));
        Inside
    }

    pub fn already() -> bool {
        INSIDE.with(|inside| inside.get() > 0)
    }
}

impl Drop for Inside {
    fn drop(&mut self) {
        INSIDE.with(|inside| inside.set(inside.get() - 1));
    }
}

// --- Buffers ---------------------------------------------------------------------------------------------------

/// Events since the flusher last took them; `base` is the time the first delta of `bytes` is from.
#[derive(Default)]
pub struct Events {
    pub bytes: Vec<u8>,
    base: i64,
    last: i64,
}

impl Events {
    fn starting_at(t: i64) -> Events {
        Events { bytes: Vec::new(), base: t, last: t }
    }

    pub fn timed(&mut self, kind: u8, t: i64) {
        self.bytes.push(kind);
        put_zigzag(&mut self.bytes, t - self.last);
        self.last = t;
    }

    /// The events and the time their first delta is from, or None when there are none.
    fn take(&mut self) -> Option<(Vec<u8>, i64)> {
        if self.bytes.is_empty() {
            return None;
        }
        let base = self.base;
        self.base = self.last;
        Some((std::mem::take(&mut self.bytes), base))
    }
}

pub struct Buffer {
    pub thread: u64,
    pub events: Mutex<Events>,
    ended: AtomicBool,
}

struct ThreadSlot {
    buffer: Option<Arc<Buffer>>,
    session: i64,
}

impl Drop for ThreadSlot {
    fn drop(&mut self) {
        if let Some(buffer) = &self.buffer {
            buffer.ended.store(true, Ordering::Release);
        }
    }
}

thread_local! {
    static SLOT: RefCell<ThreadSlot> = const { RefCell::new(ThreadSlot { buffer: None, session: 0 }) };
}

// --- The session -----------------------------------------------------------------------------------------------

#[derive(Default)]
struct Names {
    strings: HashMap<String, u64>,
    plots: HashMap<String, u64>,
    next_string: u64,
    next_site: u64,
    next_plot: u64,
    next_thread: u64,
    next_lock: u64,
    threads: Vec<Arc<Buffer>>,
}

pub struct Session {
    pub number: i64,
    start: i64,
    out: Mutex<Box<dyn Write + Send>>,
    names: Mutex<Names>,
    pub next_task: AtomicU64,
    pub global: Mutex<Events>,
    stopping: AtomicBool,
    flusher: Mutex<Option<JoinHandle<()>>>,
}

/// The session when one records, else None; the first call starts one from IED_PROFILER.
pub fn running() -> Option<&'static Session> {
    match STATE.load(Ordering::Acquire) {
        ON => return current(),
        OFF => return None,
        _ => {}
    }
    if STATE.compare_exchange(UNKNOWN, OFF, Ordering::AcqRel, Ordering::Acquire).is_ok() {
        if let Ok(target) = std::env::var("IED_PROFILER") {
            if !target.is_empty() {
                let _ = start_session(&target, &program_name());
            }
        }
    }
    if STATE.load(Ordering::Acquire) == ON {
        current()
    } else {
        None
    }
}

fn current() -> Option<&'static Session> {
    // A session is leaked once started: threads may still hold it after it stops.
    unsafe { CURRENT.load(Ordering::Acquire).as_ref() }
}

fn program_name() -> String {
    std::env::current_exe()
        .ok()
        .and_then(|path| path.file_stem().map(|stem| stem.to_string_lossy().into_owned()))
        .unwrap_or_else(|| "program".to_string())
}

pub fn start_session(target: &str, app: &str) -> Result<(), String> {
    let mut sessions = lock(&STARTING);
    if STATE.load(Ordering::Acquire) == ON {
        return Err("a profiling session is running".to_string());
    }
    let (mut out, token) = open(target)?;
    *sessions += 1;
    let start = now();
    let session = Session {
        number: *sessions,
        start,
        out: Mutex::new(Box::new(std::io::sink())),
        names: Mutex::new(Names::default()),
        next_task: AtomicU64::new(0),
        global: Mutex::new(Events::starting_at(start)),
        stopping: AtomicBool::new(false),
        flusher: Mutex::new(None),
    };
    out.write_all(&session.hello(app, &token)).map_err(|_| "the profiler closed the connection".to_string())?;
    *lock(&session.out) = out;
    session.queue_calibration(start);
    let session: &'static Session = Box::leak(Box::new(session));
    *lock(&session.flusher) = Some(std::thread::spawn(move || session.flush_loop()));
    CURRENT.store(session as *const Session as *mut Session, Ordering::Release);
    register_exit_stop();
    STATE.store(ON, Ordering::Release);
    Ok(())
}

/// Connects to "host:port;token=<t>" or creates the file of "file:<path>".
fn open(target: &str) -> Result<(Box<dyn Write + Send>, String), String> {
    if let Some(path) = target.strip_prefix("file:") {
        let file = std::fs::File::create(path).map_err(|_| format!("cannot write {path}"))?;
        return Ok((Box::new(std::io::BufWriter::new(file)), String::new()));
    }
    let (address, token) = target.split_once(";token=").unwrap_or((target, ""));
    let resolved = address.to_socket_addrs().map_err(|_| format!("not an address: {address}"))?;
    for at in resolved {
        if let Ok(stream) = TcpStream::connect_timeout(&at, CONNECT_TIMEOUT) {
            let _ = stream.set_nodelay(true);
            return Ok((Box::new(stream), token.to_string()));
        }
    }
    Err(format!("cannot reach the profiler at {address}"))
}

extern "C" {
    fn atexit(callback: extern "C" fn()) -> i32;
}

extern "C" fn stop_at_exit() {
    stop_session();
}

fn register_exit_stop() {
    static REGISTERED: AtomicBool = AtomicBool::new(false);
    if !REGISTERED.swap(true, Ordering::AcqRel) {
        // Rust runs nothing at exit; the C runtime's atexit sends the last events and the Bye.
        unsafe {
            atexit(stop_at_exit);
        }
    }
}

pub fn stop_session() {
    let _inside = Inside::enter();
    let _starting = lock(&STARTING);
    if STATE.compare_exchange(ON, OFF, Ordering::AcqRel, Ordering::Acquire).is_err() {
        return;
    }
    let Some(session) = current() else { return };
    session.stopping.store(true, Ordering::Release);
    if let Some(flusher) = lock(&session.flusher).take() {
        let _ = flusher.join();
    }
    if session.flush() {
        let mut bye = Vec::new();
        put_chunk(&mut bye, GLOBAL_CHUNK, 0, session.start, &[BYE, 0]);
        let mut out = lock(&session.out);
        let _ = out.write_all(&bye);
        let _ = out.flush();
    }
    *lock(&session.out) = Box::new(std::io::sink());
}

impl Session {
    fn hello(&self, app: &str, token: &str) -> Vec<u8> {
        let mut body = vec![HELLO];
        put_varint(&mut body, PROTOCOL_VERSION as u64);
        put_text(&mut body, app);
        put_varint(&mut body, std::process::id() as u64);
        put_varint(&mut body, TICKS_PER_SECOND);
        put_varint(&mut body, self.start as u64);
        put_varint(&mut body, unix_ns());
        put_varint(&mut body, 0);
        put_text(&mut body, token);
        let mut out = preamble();
        put_chunk(&mut out, GLOBAL_CHUNK, 0, self.start, &body);
        out
    }

    fn queue_calibration(&self, t: i64) {
        let mut global = lock(&self.global);
        global.bytes.push(CALIBRATION);
        put_varint(&mut global.bytes, t as u64);
        put_varint(&mut global.bytes, unix_ns());
    }

    fn flush_loop(&'static self) {
        let _inside = Inside::enter();
        let mut calibrated = self.start;
        while !self.stopping.load(Ordering::Acquire) {
            std::thread::sleep(FLUSH_EVERY);
            let t = now();
            if t - calibrated >= CALIBRATE_EVERY_NS {
                calibrated = t;
                self.queue_calibration(t);
            }
            if !self.flush() {
                let _ = STATE.compare_exchange(ON, OFF, Ordering::AcqRel, Ordering::Acquire);
                *lock(&self.out) = Box::new(std::io::sink());
                return;
            }
        }
    }

    /// Sends the global queue's chunk, then each thread's; false when the send failed.
    fn flush(&self) -> bool {
        let buffers = lock(&self.names).threads.clone();
        let mut threads = Vec::new();
        for buffer in &buffers {
            if let Some((body, base)) = lock(&buffer.events).take() {
                put_chunk(&mut threads, THREAD_CHUNK, buffer.thread, base, &body);
            }
        }
        let mut out = Vec::new();
        if let Some((body, base)) = lock(&self.global).take() {
            put_chunk(&mut out, GLOBAL_CHUNK, 0, base, &body);
        }
        out.extend_from_slice(&threads);
        lock(&self.names).threads.retain(|buffer| !(buffer.ended.load(Ordering::Acquire) && lock(&buffer.events).bytes.is_empty()));
        if out.is_empty() {
            return true;
        }
        let mut sink = lock(&self.out);
        sink.write_all(&out).and_then(|_| sink.flush()).is_ok()
    }

    /// The calling thread's buffer in this session, made (and its ThreadDef queued) the first time; None while the
    /// thread is being torn down.
    pub fn thread_buffer(&self) -> Option<Arc<Buffer>> {
        SLOT.try_with(|slot| {
            let mut slot = slot.try_borrow_mut().ok()?;
            if let Some(buffer) = &slot.buffer {
                if slot.session == self.number {
                    return Some(buffer.clone());
                }
            }
            let buffer = self.new_buffer();
            slot.buffer = Some(buffer.clone());
            slot.session = self.number;
            Some(buffer)
        })
        .ok()
        .flatten()
    }

    fn new_buffer(&self) -> Arc<Buffer> {
        let mut names = lock(&self.names);
        names.next_thread += 1;
        let buffer = Arc::new(Buffer { thread: names.next_thread, events: Mutex::new(Events::starting_at(self.start)), ended: AtomicBool::new(false) });
        names.threads.push(buffer.clone());
        {
            let mut global = lock(&self.global);
            global.bytes.push(THREAD_DEF);
            put_varint(&mut global.bytes, buffer.thread);
            put_varint(&mut global.bytes, os_thread_id());
            put_varint(&mut global.bytes, 0);
        }
        if let Some(name) = std::thread::current().name() {
            let id = self.intern_locked(&mut names, name);
            drop(names);
            let mut events = lock(&buffer.events);
            events.timed(THREAD_NAME, now());
            put_varint(&mut events.bytes, id);
        }
        buffer
    }

    /// The id of `text`, its StringDef queued the first time.
    pub fn intern(&self, text: &str) -> u64 {
        let mut names = lock(&self.names);
        self.intern_locked(&mut names, text)
    }

    fn intern_locked(&self, names: &mut Names, text: &str) -> u64 {
        if text.is_empty() {
            return 0;
        }
        if let Some(&id) = names.strings.get(text) {
            return id;
        }
        let id = self.once_locked(names, text);
        names.strings.insert(text.to_string(), id);
        id
    }

    /// A fresh id for text sent once (a message, a zone's text).
    pub fn once(&self, text: &str) -> u64 {
        let mut names = lock(&self.names);
        self.once_locked(&mut names, text)
    }

    fn once_locked(&self, names: &mut Names, text: &str) -> u64 {
        names.next_string += 1;
        let id = names.next_string;
        let mut global = lock(&self.global);
        global.bytes.push(STRING_DEF);
        put_varint(&mut global.bytes, id);
        put_text(&mut global.bytes, text);
        id
    }

    /// The id of a source location, its SrcLocDef queued the first time in this session.
    pub fn site_id(&self, site: &SiteIds, name: &str, function: &str, file: &str, line: u32) -> u64 {
        if site.session.load(Ordering::Acquire) == self.number {
            return site.id.load(Ordering::Relaxed);
        }
        let mut names = lock(&self.names);
        if site.session.load(Ordering::Acquire) == self.number {
            return site.id.load(Ordering::Relaxed);
        }
        let name_id = self.intern_locked(&mut names, name);
        let function_id = self.intern_locked(&mut names, function);
        let file_id = self.intern_locked(&mut names, file);
        names.next_site += 1;
        let id = names.next_site;
        {
            let mut global = lock(&self.global);
            global.bytes.push(SRC_LOC_DEF);
            for value in [id, name_id, function_id, file_id, line as u64, 0] {
                put_varint(&mut global.bytes, value);
            }
        }
        site.id.store(id, Ordering::Relaxed);
        site.session.store(self.number, Ordering::Release);
        id
    }

    /// The id of a lock, its place and its LockDef queued the first time in this session.
    pub fn lock_id(&self, ids: &SiteIds, name: &str, file: &str, line: u32) -> u64 {
        if ids.session.load(Ordering::Acquire) == self.number {
            return ids.id.load(Ordering::Relaxed);
        }
        let mut names = lock(&self.names);
        if ids.session.load(Ordering::Acquire) == self.number {
            return ids.id.load(Ordering::Relaxed);
        }
        let name_id = self.intern_locked(&mut names, name);
        let file_id = self.intern_locked(&mut names, file);
        names.next_site += 1;
        let site = names.next_site;
        names.next_lock += 1;
        let id = names.next_lock;
        {
            let mut global = lock(&self.global);
            global.bytes.push(SRC_LOC_DEF);
            for value in [site, name_id, 0, file_id, line as u64, 0] {
                put_varint(&mut global.bytes, value);
            }
            global.bytes.push(LOCK_DEF);
            for value in [id, name_id, site] {
                put_varint(&mut global.bytes, value);
            }
        }
        ids.id.store(id, Ordering::Relaxed);
        ids.session.store(self.number, Ordering::Release);
        id
    }

    /// The id of the plot `name`, its PlotDef queued the first time.
    pub fn plot_id(&self, name: &str) -> u64 {
        let mut names = lock(&self.names);
        if let Some(&id) = names.plots.get(name) {
            return id;
        }
        let name_id = self.intern_locked(&mut names, name);
        names.next_plot += 1;
        let id = names.next_plot;
        names.plots.insert(name.to_string(), id);
        let mut global = lock(&self.global);
        global.bytes.push(PLOT_DEF);
        for value in [id, name_id, 0, 0] {
            put_varint(&mut global.bytes, value);
        }
        id
    }

    /// Appends a timed event of the calling thread.
    pub fn thread_event(&self, kind: u8, fields: &[u64]) {
        let Some(buffer) = self.thread_buffer() else { return };
        let t = now();
        let mut events = lock(&buffer.events);
        events.timed(kind, t);
        for &field in fields {
            put_varint(&mut events.bytes, field);
        }
    }

    /// Appends an untimed event of the calling thread (a zone's text or value).
    pub fn thread_detail(&self, kind: u8, field: u64) {
        let Some(buffer) = self.thread_buffer() else { return };
        let mut events = lock(&buffer.events);
        events.bytes.push(kind);
        put_varint(&mut events.bytes, field);
    }

    pub fn global(&self) -> MutexGuard<'_, Events> {
        lock(&self.global)
    }
}

/// A source location's id in the session that registered it.
pub struct SiteIds {
    pub id: AtomicU64,
    pub session: AtomicI64,
}

impl SiteIds {
    pub const fn new() -> SiteIds {
        SiteIds { id: AtomicU64::new(0), session: AtomicI64::new(0) }
    }
}

#[cfg(windows)]
fn os_thread_id() -> u64 {
    extern "system" {
        fn GetCurrentThreadId() -> u32;
    }
    unsafe { GetCurrentThreadId() as u64 }
}

#[cfg(not(windows))]
fn os_thread_id() -> u64 {
    use std::hash::{Hash, Hasher};
    let mut hasher = std::collections::hash_map::DefaultHasher::new();
    std::thread::current().id().hash(&mut hasher);
    hasher.finish()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn taking_events_moves_the_base_to_the_last_event() {
        let mut events = Events::starting_at(100);
        events.timed(ZONE_BEGIN, 150);
        events.timed(ZONE_END, 140);
        assert_eq!(Some((vec![ZONE_BEGIN, 100, ZONE_END, 19], 100)), events.take());
        assert_eq!(None, events.take());
        events.timed(ZONE_END, 160);
        assert_eq!(140, events.take().unwrap().1);
    }

    #[test]
    fn malformed_targets_are_refused() {
        let missing = std::env::temp_dir().join("ied-profiler-missing").join("x.iedtrace");
        for target in ["no-port".to_string(), format!("file:{}", missing.display())] {
            assert!(open(&target).is_err(), "{target}");
        }
    }
}

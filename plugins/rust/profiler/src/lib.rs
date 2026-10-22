//! The Rust client of the IED Trace Protocol (IED docs/16). A program run with `IED_PROFILER` set streams its zones,
//! frames, plots, messages, thread names, tasks and allocations to the IED profiler (IED's Profile task sets it to
//! `127.0.0.1:<port>;token=<t>`), or with `file:<path>` to a trace file that IED opens. Without it every call
//! returns after one atomic load.
//!
//! ```no_run
//! fn update(entities: usize) {
//!     let _zone = ied_profiler::zone!("Update");        // to the end of the scope, at this file and line
//!     ied_profiler::plot("Entities", entities as f64);
//! }
//!
//! fn main() {
//!     for _ in 0..1000 {
//!         update(2048);
//!         ied_profiler::frame();                         // once per presented frame
//!     }
//! }
//! ```
//!
//! Threads keep the names std gives them ("main", a `Builder::name`); `set_thread_name` renames one from then on.
//! `Mutex` (or `mutex!` in a static) is a std mutex whose waits and holds the profiler shows; `Tracked` reports every
//! allocation of the program's global allocator; the `tracing` feature turns spans and events of the `tracing`
//! ecosystem into zones and messages. The session ends at exit (or with `stop`).

mod encode;
mod locks;
mod session;
#[cfg(feature = "tracing")]
pub mod tracing;

use encode::*;
pub use locks::{LockSite, Mutex, MutexGuard};
use session::{running, Inside, Session, SiteIds};
use std::alloc::{GlobalAlloc, Layout};
use std::marker::PhantomData;

/// Severity of a `message`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Severity {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5,
}

/// Starts recording to `target` ("host:port;token=<t>" or "file:<path>") as `app`. A program run by IED does not
/// call it: its first call starts from `IED_PROFILER`.
pub fn start(target: &str, app: &str) -> Result<(), String> {
    let _inside = Inside::enter();
    running();
    session::start_session(target, app)
}

/// Sends what is buffered and ends the session (also done at exit).
pub fn stop() {
    session::stop_session();
}

/// Whether a session records.
pub fn is_active() -> bool {
    let _inside = Inside::enter();
    running().is_some()
}

// --- Zones -----------------------------------------------------------------------------------------------------

/// A source location: where a zone is. `zone!` makes a static one.
pub struct Site {
    name: &'static str,
    function: Function,
    file: &'static str,
    line: u32,
    ids: SiteIds,
}

enum Function {
    // Computed when the site is first registered (`zone!` finds its enclosing function's path at run time).
    Lazy(fn() -> &'static str),
    Known(&'static str),
}

impl Site {
    pub const fn new(name: &'static str, function: fn() -> &'static str, file: &'static str, line: u32) -> Site {
        Site { name, function: Function::Lazy(function), file, line, ids: SiteIds::new() }
    }

    /// A site whose function is already known (a `tracing` callsite's module).
    pub const fn known(name: &'static str, function: &'static str, file: &'static str, line: u32) -> Site {
        Site { name, function: Function::Known(function), file, line, ids: SiteIds::new() }
    }

    fn id(&self, session: &Session) -> u64 {
        let function = match self.function {
            Function::Lazy(compute) => compute(),
            Function::Known(function) => function,
        };
        session.site_id(&self.ids, self.name, function, self.file, self.line)
    }
}

/// An open zone of the calling thread, closed when dropped. Bind it to a name (`let _zone = ...`): `let _ = ...`
/// drops it at once.
#[must_use = "a zone ends when it is dropped"]
pub struct Zone {
    open: bool,
    _thread_bound: PhantomData<*const ()>,
}

impl Zone {
    pub fn new(site: &'static Site) -> Zone {
        let _inside = Inside::enter();
        let Some(session) = running() else { return Zone::closed() };
        let id = site.id(session);
        session.thread_event(ZONE_BEGIN, &[id]);
        Zone { open: true, _thread_bound: PhantomData }
    }

    /// A zone at `site` with a name given per call (a file being loaded).
    pub fn named(site: &'static Site, name: &str) -> Zone {
        let _inside = Inside::enter();
        let Some(session) = running() else { return Zone::closed() };
        let id = site.id(session);
        let text = session.intern(name);
        session.thread_event(ZONE_BEGIN_NAMED, &[id, text]);
        Zone { open: true, _thread_bound: PhantomData }
    }

    fn closed() -> Zone {
        Zone { open: false, _thread_bound: PhantomData }
    }

    /// Attaches text to the zone (shown in its details).
    pub fn text(&self, text: &str) {
        let _inside = Inside::enter();
        if let (true, Some(session)) = (self.open, running()) {
            let id = session.once(text);
            session.thread_detail(ZONE_TEXT, id);
        }
    }

    /// Attaches a number to the zone.
    pub fn value(&self, value: u64) {
        let _inside = Inside::enter();
        if let (true, Some(session)) = (self.open, running()) {
            session.thread_detail(ZONE_VALUE, value);
        }
    }
}

impl Drop for Zone {
    fn drop(&mut self) {
        let _inside = Inside::enter();
        if let (true, Some(session)) = (self.open, running()) {
            session.thread_event(ZONE_END, &[]);
        }
    }
}

/// A zone from here to the end of the scope: `let _zone = zone!("Update");`, or `zone!(site_name, run_time_name)`
/// for a name given per call.
#[macro_export]
macro_rules! zone {
    ($name:expr) => {{
        static SITE: $crate::Site = $crate::Site::new($name, $crate::__function!(), file!(), line!());
        $crate::Zone::new(&SITE)
    }};
    ($site:expr, $name:expr) => {{
        static SITE: $crate::Site = $crate::Site::new($site, $crate::__function!(), file!(), line!());
        $crate::Zone::named(&SITE, $name)
    }};
}

/// The enclosing function's path, computed when a site is first registered.
#[doc(hidden)]
#[macro_export]
macro_rules! __function {
    () => {{
        fn function() -> &'static str {
            fn here() {}
            $crate::__enclosing(::std::any::type_name_of_val(&here))
        }
        function
    }};
}

// "demo::update::{{closure}}::SITE::function::here" is a zone in a closure of demo::update.
#[doc(hidden)]
pub fn __enclosing(path: &'static str) -> &'static str {
    let mut path = path.strip_suffix("::function::here").unwrap_or(path);
    path = path.strip_suffix("::SITE").unwrap_or(path);
    while let Some(outer) = path.strip_suffix("::{{closure}}") {
        path = outer;
    }
    path
}

// --- Frames, plots, messages, threads --------------------------------------------------------------------------

/// The end of a frame of the default set.
pub fn frame() {
    frame_event(FRAME_MARK, "");
}

/// The end of a frame of the set `set`.
pub fn frame_named(set: &str) {
    frame_event(FRAME_MARK, set);
}

/// The bounds of a discontinuous frame of the set `set` (a level load).
pub fn frame_begin(set: &str) {
    frame_event(FRAME_BEGIN, set);
}

pub fn frame_end(set: &str) {
    frame_event(FRAME_END, set);
}

fn frame_event(kind: u8, set: &str) {
    let _inside = Inside::enter();
    if let Some(session) = running() {
        let id = session.intern(set);
        session.thread_event(kind, &[id]);
    }
}

/// A value of the series `name`.
pub fn plot(name: &str, value: f64) {
    let _inside = Inside::enter();
    let Some(session) = running() else { return };
    let id = session.plot_id(name);
    let Some(buffer) = session.thread_buffer() else { return };
    let t = session::now();
    let mut events = buffer.events.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
    events.timed(PLOT_VALUE, t);
    put_varint(&mut events.bytes, id);
    put_double(&mut events.bytes, value);
}

/// A log line on the calling thread's track.
pub fn message(severity: Severity, text: &str) {
    let _inside = Inside::enter();
    if let Some(session) = running() {
        let id = session.once(text);
        session.thread_event(MESSAGE, &[severity as u64, id, 0]);
    }
}

/// Names the calling thread from now on; call it again whenever the thread takes another role.
pub fn set_thread_name(name: &str) {
    let _inside = Inside::enter();
    if let Some(session) = running() {
        let id = session.intern(name);
        session.thread_event(THREAD_NAME, &[id]);
    }
}

// --- Tasks -----------------------------------------------------------------------------------------------------

/// A unit of work the profiler's Tasks view groups by name, wherever it runs; ended when dropped. A task that moves
/// to another thread calls `suspend` there and `resume` on the thread it continues on.
pub struct Task {
    pub id: u64,
}

impl Task {
    pub fn begin(name: &str, parent: Option<&Task>) -> Task {
        let _inside = Inside::enter();
        let Some(session) = running() else { return Task { id: 0 } };
        let text = session.intern(name);
        let id = session.next_task.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
        session.thread_event(TASK_BEGIN, &[id, text, parent.map_or(0, |p| p.id)]);
        Task { id }
    }

    pub fn suspend(&self) {
        task_event(TASK_SUSPEND, self.id);
    }

    pub fn resume(&self) {
        task_event(TASK_RESUME, self.id);
    }
}

impl Drop for Task {
    fn drop(&mut self) {
        task_event(TASK_END, self.id);
    }
}

fn task_event(kind: u8, task: u64) {
    let _inside = Inside::enter();
    if let (true, Some(session)) = (task != 0, running()) {
        session.thread_event(kind, &[task]);
    }
}

// --- Memory ----------------------------------------------------------------------------------------------------

/// A block of the program's pool `pool` (pool 0 is the global allocator's).
pub fn alloc(address: usize, size: usize, pool: u32) {
    memory_event(ALLOC, address, size, pool);
}

pub fn free(address: usize, pool: u32) {
    memory_event(FREE, address, 0, pool);
}

pub fn pool_name(pool: u32, name: &str) {
    let _inside = Inside::enter();
    let Some(session) = running() else { return };
    let text = session.intern(name);
    let mut global = session.global();
    global.bytes.push(POOL_DEF);
    put_varint(&mut global.bytes, pool as u64);
    put_varint(&mut global.bytes, text);
}

/// Every block of `pool` freed at once (an arena reset).
pub fn pool_reset(pool: u32) {
    let _inside = Inside::enter();
    let Some(session) = running() else { return };
    let t = session::now();
    let mut global = session.global();
    global.timed(POOL_RESET, t);
    put_varint(&mut global.bytes, pool as u64);
}

// Memory events go to the global queue, so a block freed on another thread is never seen before its allocation.
fn memory_event(kind: u8, address: usize, size: usize, pool: u32) {
    if Inside::already() {
        return;
    }
    let _inside = Inside::enter();
    let Some(session) = running() else { return };
    let thread = session.thread_buffer().map_or(0, |buffer| buffer.thread);
    let t = session::now();
    let mut global = session.global();
    global.timed(kind, t);
    put_varint(&mut global.bytes, thread);
    put_varint(&mut global.bytes, pool as u64);
    put_varint(&mut global.bytes, address as u64);
    if kind == ALLOC {
        put_varint(&mut global.bytes, size as u64);
        put_varint(&mut global.bytes, 0);
    }
}

/// A global allocator that reports every block to the profiler (pool 0):
/// `#[global_allocator] static ALLOC: ied_profiler::Tracked<std::alloc::System> = ied_profiler::Tracked(std::alloc::System);`
pub struct Tracked<A>(pub A);

unsafe impl<A: GlobalAlloc> GlobalAlloc for Tracked<A> {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let block = self.0.alloc(layout);
        if !block.is_null() {
            alloc(block as usize, layout.size(), 0);
        }
        block
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        let block = self.0.alloc_zeroed(layout);
        if !block.is_null() {
            alloc(block as usize, layout.size(), 0);
        }
        block
    }

    unsafe fn dealloc(&self, block: *mut u8, layout: Layout) {
        free(block as usize, 0);
        self.0.dealloc(block, layout);
    }

    unsafe fn realloc(&self, block: *mut u8, layout: Layout, size: usize) -> *mut u8 {
        let moved = self.0.realloc(block, layout, size);
        if !moved.is_null() {
            free(block as usize, 0);
            alloc(moved as usize, size, 0);
        }
        moved
    }
}

#[cfg(test)]
mod tests {
    use super::{Function, Site};

    fn function_of(site: &Site) -> &'static str {
        match site.function {
            Function::Lazy(compute) => compute(),
            Function::Known(function) => function,
        }
    }

    #[test]
    fn a_site_names_its_enclosing_function() {
        static SITE: Site = Site::new("Zone", crate::__function!(), file!(), line!());
        assert_eq!("ied_profiler::tests::a_site_names_its_enclosing_function", function_of(&SITE));
        let in_closure = || {
            static SITE: Site = Site::new("Zone", crate::__function!(), file!(), line!());
            function_of(&SITE)
        };
        assert_eq!("ied_profiler::tests::a_site_names_its_enclosing_function", in_closure());
    }
}

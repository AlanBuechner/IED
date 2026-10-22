// Locks (IED docs/16 "Locks"): a std::sync::Mutex the profiler watches, and the site any other lock reports through.

use crate::encode::{LOCK_OBTAIN, LOCK_RELEASE, LOCK_WAIT};
use crate::session::{running, Inside, SiteIds};
use std::ops::{Deref, DerefMut};
use std::sync::{MutexGuard as StdGuard, TryLockError};

/// A lock as the profiler shows it: its name and where it was made, registered once per session. For a lock of the
/// program's own, report its waits and holds through `wait`, `obtained` and `released`.
pub struct LockSite {
    name: &'static str,
    file: &'static str,
    line: u32,
    ids: SiteIds,
}

impl LockSite {
    pub const fn new(name: &'static str, file: &'static str, line: u32) -> LockSite {
        LockSite { name, file, line, ids: SiteIds::new() }
    }

    /// The calling thread starts waiting for the lock (`shared` for a reader of a reader-writer lock).
    pub fn wait(&self, shared: bool) {
        self.event(LOCK_WAIT, shared);
    }

    pub fn obtained(&self, shared: bool) {
        self.event(LOCK_OBTAIN, shared);
    }

    pub fn released(&self, shared: bool) {
        self.event(LOCK_RELEASE, shared);
    }

    fn event(&self, kind: u8, shared: bool) {
        let _inside = Inside::enter();
        if let Some(session) = running() {
            let id = session.lock_id(&self.ids, self.name, self.file, self.line);
            session.thread_event(kind, &[id * 2 + shared as u64]);
        }
    }
}

/// A std::sync::Mutex the profiler watches: its waits and holds show on each thread's lock rows, its contention in the
/// Locks window. A free mutex is taken without recording a wait; a poisoned one is taken all the same.
pub struct Mutex<T> {
    inner: std::sync::Mutex<T>,
    site: LockSite,
}

impl<T> Mutex<T> {
    /// A mutex named `name`, at the caller's file and line.
    #[track_caller]
    pub fn new(name: &'static str, value: T) -> Mutex<T> {
        let at = std::panic::Location::caller();
        Mutex::at(name, at.file(), at.line(), value)
    }

    /// A mutex at a given place (`mutex!` makes one in a static).
    pub const fn at(name: &'static str, file: &'static str, line: u32, value: T) -> Mutex<T> {
        Mutex { inner: std::sync::Mutex::new(value), site: LockSite::new(name, file, line) }
    }

    pub fn lock(&self) -> MutexGuard<'_, T> {
        let guard = match self.inner.try_lock() {
            Ok(guard) => guard,
            Err(TryLockError::Poisoned(poisoned)) => poisoned.into_inner(),
            Err(TryLockError::WouldBlock) => {
                self.site.wait(false);
                self.inner.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
            }
        };
        self.site.obtained(false);
        MutexGuard { guard: Some(guard), site: &self.site }
    }

    pub fn try_lock(&self) -> Option<MutexGuard<'_, T>> {
        let guard = match self.inner.try_lock() {
            Ok(guard) => guard,
            Err(TryLockError::Poisoned(poisoned)) => poisoned.into_inner(),
            Err(TryLockError::WouldBlock) => return None,
        };
        self.site.obtained(false);
        Some(MutexGuard { guard: Some(guard), site: &self.site })
    }
}

/// A held `Mutex`: the release is recorded, then the lock let go, when it is dropped.
pub struct MutexGuard<'a, T> {
    guard: Option<StdGuard<'a, T>>,
    site: &'a LockSite,
}

impl<T> Deref for MutexGuard<'_, T> {
    type Target = T;

    fn deref(&self) -> &T {
        self.guard.as_ref().expect("held until dropped")
    }
}

impl<T> DerefMut for MutexGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut T {
        self.guard.as_mut().expect("held until dropped")
    }
}

impl<T> Drop for MutexGuard<'_, T> {
    fn drop(&mut self) {
        self.site.released(false);
        drop(self.guard.take());
    }
}

/// A `Mutex` at this file and line, usable in a static: `static QUEUE: ied_profiler::Mutex<Vec<u32>> = ied_profiler::mutex!("Queue", Vec::new());`
#[macro_export]
macro_rules! mutex {
    ($name:expr, $value:expr) => {
        $crate::Mutex::at($name, file!(), line!(), $value)
    };
}

//! Spans and events of the `tracing` ecosystem as zones and messages:
//! `tracing::subscriber::set_global_default(ied_profiler::tracing::Subscriber::new())`.
//!
//! Entering a span opens a zone at its callsite, named after the span with its fields as the zone's text; leaving it
//! closes the zone, so an async task's polls are zones on the threads that ran them. An event is a message of its
//! level. It is a whole subscriber (it composes with no other).

use crate::{Severity, Site, Zone};
use std::cell::RefCell;
use std::collections::HashMap;
use std::fmt::{Debug, Write};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, MutexGuard};
use tracing_core::field::{Field, Visit};
use tracing_core::span::{Attributes, Id, Record};
use tracing_core::{Event, Interest, Level, Metadata};

thread_local! {
    // The zones of the spans the calling thread is in, innermost last.
    static ENTERED: RefCell<Vec<(u64, Zone)>> = const { RefCell::new(Vec::new()) };
}

struct Span {
    site: &'static Site,
    text: String,
    references: usize,
}

#[derive(Default)]
pub struct Subscriber {
    next: AtomicU64,
    spans: Mutex<HashMap<u64, Span>>,
    sites: Mutex<HashMap<usize, &'static Site>>,
}

impl Subscriber {
    pub fn new() -> Subscriber {
        Subscriber::default()
    }

    fn spans(&self) -> MutexGuard<'_, HashMap<u64, Span>> {
        self.spans.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    // One site per callsite, kept for the program's life like the callsite's own metadata.
    fn site(&self, metadata: &'static Metadata<'static>) -> &'static Site {
        let mut sites = self.sites.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        sites.entry(metadata as *const Metadata<'static> as usize).or_insert_with(|| {
            let function = metadata.module_path().unwrap_or(metadata.target());
            Box::leak(Box::new(Site::known(metadata.name(), function, metadata.file().unwrap_or(""), metadata.line().unwrap_or(0))))
        })
    }
}

/// Fields as text: an event's message first, then name=value pairs.
#[derive(Default)]
struct Text(String);

impl Visit for Text {
    fn record_debug(&mut self, field: &Field, value: &dyn Debug) {
        if !self.0.is_empty() {
            self.0.push(' ');
        }
        if field.name() == "message" {
            let _ = write!(self.0, "{value:?}");
        } else {
            let _ = write!(self.0, "{}={value:?}", field.name());
        }
    }

    fn record_str(&mut self, field: &Field, value: &str) {
        if field.name() == "message" {
            self.record_debug(field, &format_args!("{value}"));
        } else {
            self.record_debug(field, &value);
        }
    }
}

fn severity(level: &Level) -> Severity {
    match *level {
        Level::TRACE => Severity::Trace,
        Level::DEBUG => Severity::Debug,
        Level::INFO => Severity::Info,
        Level::WARN => Severity::Warning,
        _ => Severity::Error,
    }
}

impl tracing_core::Subscriber for Subscriber {
    // Asked again each time, so spans record once a session starts later.
    fn register_callsite(&self, _: &'static Metadata<'static>) -> Interest {
        Interest::sometimes()
    }

    fn enabled(&self, _: &Metadata<'_>) -> bool {
        crate::is_active()
    }

    fn new_span(&self, span: &Attributes<'_>) -> Id {
        let id = self.next.fetch_add(1, Ordering::Relaxed) + 1;
        let mut text = Text::default();
        span.record(&mut text);
        let site = self.site(span.metadata());
        self.spans().insert(id, Span { site, text: text.0, references: 1 });
        Id::from_u64(id)
    }

    fn record(&self, span: &Id, values: &Record<'_>) {
        if let Some(found) = self.spans().get_mut(&span.into_u64()) {
            let mut text = Text(std::mem::take(&mut found.text));
            values.record(&mut text);
            found.text = text.0;
        }
    }

    fn record_follows_from(&self, _: &Id, _: &Id) {}

    fn event(&self, event: &Event<'_>) {
        let mut text = Text::default();
        event.record(&mut text);
        crate::message(severity(event.metadata().level()), &text.0);
    }

    fn enter(&self, span: &Id) {
        let Some((site, text)) = self.spans().get(&span.into_u64()).map(|found| (found.site, found.text.clone())) else { return };
        let zone = Zone::new(site);
        if !text.is_empty() {
            zone.text(&text);
        }
        let _ = ENTERED.try_with(|entered| entered.borrow_mut().push((span.into_u64(), zone)));
    }

    fn exit(&self, span: &Id) {
        let id = span.into_u64();
        let _ = ENTERED.try_with(|entered| {
            let mut entered = entered.borrow_mut();
            if let Some(at) = entered.iter().rposition(|(open, _)| *open == id) {
                drop(entered.remove(at));
            }
        });
    }

    fn clone_span(&self, span: &Id) -> Id {
        if let Some(found) = self.spans().get_mut(&span.into_u64()) {
            found.references += 1;
        }
        span.clone()
    }

    fn try_close(&self, span: Id) -> bool {
        let mut spans = self.spans();
        let Some(found) = spans.get_mut(&span.into_u64()) else { return false };
        found.references -= 1;
        if found.references > 0 {
            return false;
        }
        spans.remove(&span.into_u64());
        true
    }
}

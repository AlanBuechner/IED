"""The scale test's program (assets/ScaleTest/generate.py) in Rust, for the build-speed comparison: one crate, one
module per scale-test module (a record, a ledger over a Vec and a HashMap, a generic, a boxed chain, twelve
arithmetic steps and a work function that calls the module before it), and a main that checks every module's work on
several threads. Arithmetic wraps, as C4's unsigned arithmetic does.

    python rust.py <output folder> [modules]
"""
import os
import sys

STEPS = 12

MIX = '''// The integer types the modules' generic mixes.
pub trait Mixed: Copy + std::ops::BitXor<Output = Self> {
    fn wrapping_mul(self, other: Self) -> Self;
    fn wrapping_add(self, other: Self) -> Self;
    fn of(value: u64) -> Self;
}

impl Mixed for u64 {
    fn wrapping_mul(self, other: Self) -> Self { u64::wrapping_mul(self, other) }
    fn wrapping_add(self, other: Self) -> Self { u64::wrapping_add(self, other) }
    fn of(value: u64) -> Self { value }
}

impl Mixed for u32 {
    fn wrapping_mul(self, other: Self) -> Self { u32::wrapping_mul(self, other) }
    fn wrapping_add(self, other: Self) -> Self { u32::wrapping_add(self, other) }
    fn of(value: u64) -> Self { value as u32 }
}
'''


def step(i, k):
    mul = 6364136223846793005 + 2 * (i * STEPS + k)
    add = 1442695040888963407 + i * 977 + k * 131
    rounds = 3 + (i + k) % 5
    return f'''pub fn step{k}(x: u64) -> u64 {{
    let mut acc = x ^ {add}u64;
    for r in 0..{rounds}u64 {{
        acc = acc.wrapping_mul({mul}u64).wrapping_add(r);
        if acc & 7 == {k % 8} {{
            acc = acc ^ (acc >> 13);
        }} else {{
            acc = acc.wrapping_add({(i * 7 + k) % 1000 + 1});
        }}
    }}
    match acc % 3 {{
        0 => acc = acc ^ {i + 1},
        1 => acc = acc.wrapping_add(acc >> 7),
        _ => acc = acc.wrapping_mul(3),
    }}
    acc
}}
'''


def module(i):
    steps = '\n'.join(step(i, k) for k in range(STEPS))
    adds = '\n'.join(f'    ledger.add(step{k}(seed.wrapping_add({k})));' for k in range(STEPS))
    recurse = f'    if seed > 64 {{\n        sum = sum.wrapping_add(crate::m{i - 1}::work(seed / 2));\n    }}\n' if i > 0 else ''
    return f'''// Module {i} of the build-speed comparison (Benchmarks/buildspeed/rust.py writes it).
use crate::mix::Mixed;
use std::collections::HashMap;
use std::fmt::Write;

pub struct Record {{
    pub id: i64,
    pub value: u64,
    pub label: String,
}}

pub struct Holder {{
    pub value: u64,
    pub next: Option<Box<Holder>>,
}}

pub struct Ledger {{
    records: Vec<Record>,
    index: HashMap<String, i64>,
    seed: u64,
}}

impl Ledger {{
    pub fn new(seed: u64) -> Ledger {{ Ledger {{ records: Vec::new(), index: HashMap::new(), seed }} }}

    pub fn add(&mut self, value: u64) {{
        let mut label = String::new();
        label.push_str("m{i}-");
        write!(label, "{{}}", value % 1000).unwrap();
        let record = Record {{ id: self.records.len() as i64, value, label }};
        self.index.insert(record.label.clone(), record.id);
        self.records.push(record);
    }}

    pub fn total(&self) -> u64 {{
        let mut total = self.seed;
        for record in &self.records {{
            total = total.wrapping_mul(1099511628211) ^ record.value;
        }}
        total
    }}

    pub fn describe(&self) -> String {{
        let mut text = String::new();
        for record in &self.records {{
            text.push_str(&record.label);
            text.push(';');
        }}
        text
    }}

    pub fn count(&self) -> i64 {{ self.records.len() as i64 }}

    pub fn has(&self, label: &str) -> bool {{ self.index.contains_key(label) }}
}}

pub fn mix<T: Mixed>(a: T, b: T) -> T {{ (a ^ b).wrapping_mul(T::of({2 * i + 1})).wrapping_add(T::of({i})) }}

{steps}
// Every step into a ledger, its text, two instances of the generic, a boxed chain, and the module before.
pub fn work(seed: u64) -> u64 {{
    let mut ledger = Ledger::new(seed);
{adds}
    let mut sum = ledger.total();
    let text = ledger.describe();
    sum = sum.wrapping_add(text.len() as u64);
    sum = mix::<u64>(sum, ledger.count() as u64);
    sum = sum.wrapping_add(mix::<u32>(sum as u32, {i}) as u64);
    if ledger.has("m{i}-0") {{
        sum = sum ^ 1;
    }}
    let mut chain: Option<Box<Holder>> = None;
    for k in 0..4u64 {{
        chain = Some(Box::new(Holder {{ value: sum.wrapping_add(k), next: chain }}));
    }}
    let mut held = chain.as_ref();
    while let Some(node) = held {{
        sum = sum ^ (node.value >> 3);
        held = node.next.as_ref();
    }}
{recurse}    sum
}}
'''


def main_source(modules):
    mods = '\n'.join(f'mod m{i};' for i in range(modules))
    calls = '\n'.join(f'    sum = sum.wrapping_mul(31).wrapping_add(m{i}::work(seed));' for i in range(modules))
    return f'''// The build-speed comparison's entry: every module's work on SCALE_THREADS threads for SCALE_SECONDS seconds, each
// result checked against the one computed first. Exit code: the wrong results.
mod mix;
{mods}

use std::sync::atomic::{{AtomicI64, Ordering}};
use std::sync::Arc;
use std::time::{{Duration, Instant}};

fn run_all(seed: u64) -> u64 {{
    let mut sum = 0u64;
{calls}
    sum
}}

fn setting(name: &str, fallback: u64) -> u64 {{
    std::env::var(name).ok().and_then(|text| text.parse().ok()).unwrap_or(fallback)
}}

fn main() {{
    let seconds = setting("SCALE_SECONDS", 5);
    let threads = setting("SCALE_THREADS", 4);
    let mut expected = [0u64; 8];
    for (s, slot) in expected.iter_mut().enumerate() {{
        *slot = run_all(12345 + s as u64);
    }}
    let runs = Arc::new(AtomicI64::new(0));
    let wrong = Arc::new(AtomicI64::new(0));
    let start = Instant::now();
    let deadline = start + Duration::from_secs(seconds);
    let workers: Vec<_> = (0..threads)
        .map(|t| {{
            let (runs, wrong) = (runs.clone(), wrong.clone());
            std::thread::spawn(move || {{
                let mut i = t;
                while Instant::now() < deadline {{
                    let s = (i % 8) as usize;
                    if run_all(12345 + s as u64) != expected[s] {{
                        wrong.fetch_add(1, Ordering::Relaxed);
                    }}
                    runs.fetch_add(1, Ordering::Relaxed);
                    i += 1;
                }}
            }})
        }})
        .collect();
    for worker in workers {{
        worker.join().unwrap();
    }}
    println!("scale: {{}} runs of {modules} modules on {{}} threads in {{}} ms, {{}} wrong", runs.load(Ordering::Relaxed), threads,
        start.elapsed().as_millis(), wrong.load(Ordering::Relaxed));
    std::process::exit(wrong.load(Ordering::Relaxed) as i32);
}}
'''


CARGO = '''[package]
name = "scale"
version = "0.1.0"
edition = "2021"

[dependencies]
'''


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def generate(folder, modules):
    write(os.path.join(folder, 'Cargo.toml'), CARGO)
    write(os.path.join(folder, 'src', 'mix.rs'), MIX)
    lines = MIX.count('\n')
    for i in range(modules):
        text = module(i)
        write(os.path.join(folder, 'src', f'm{i}.rs'), text)
        lines += text.count('\n')
    text = main_source(modules)
    write(os.path.join(folder, 'src', 'main.rs'), text)
    return lines + text.count('\n')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    print(f'{generate(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 200)} lines')

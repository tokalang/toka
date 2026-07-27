// Without a Cell-like wrapper, Rust rejects mutation through a shared view.

struct Counter {
    reads: i32,
}

fn record(shared: &Counter) {
    shared.reads += 1;
}

fn main() {
    let counter = Counter { reads: 0 };
    record(&counter);
}

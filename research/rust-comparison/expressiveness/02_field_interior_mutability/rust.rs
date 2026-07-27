// Rust represents the independently mutable field with std::cell::Cell.

use std::cell::Cell;

struct Counter {
    reads: Cell<i32>,
    ordinary: i32,
}

fn record(shared: &Counter) {
    shared.reads.set(shared.reads.get() + 1);
}

fn main() {
    let counter = Counter {
        reads: Cell::new(0),
        ordinary: 9,
    };
    let shared = &counter;

    record(shared);

    assert_eq!(shared.reads.get(), 1);
    assert_eq!(counter.reads.get(), 1);
    assert_eq!(shared.ordinary, 9);
}

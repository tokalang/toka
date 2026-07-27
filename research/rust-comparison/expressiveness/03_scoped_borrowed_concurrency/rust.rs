// Rust's standard scoped threads permit a child to borrow `value` because the
// scope joins children before `value` can leave its lexical scope.

use std::thread;

fn main() {
    let value = 7;
    let mut observed = 0;

    thread::scope(|scope| {
        let child = scope.spawn(|| value);
        observed = child.join().expect("scoped child panicked");
    });

    assert_eq!(observed, 7);
}

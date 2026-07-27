use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, Ordering};

struct Cleanup<'a>(&'a AtomicBool);

impl Drop for Cleanup<'_> {
    fn drop(&mut self) {
        self.0.store(true, Ordering::SeqCst);
    }
}

fn main() {
    let cleaned = AtomicBool::new(false);
    let outcome = catch_unwind(AssertUnwindSafe(|| {
        let _cleanup = Cleanup(&cleaned);
        panic!("comparison panic");
    }));

    assert!(outcome.is_err());
    assert!(cleaned.load(Ordering::SeqCst));
}

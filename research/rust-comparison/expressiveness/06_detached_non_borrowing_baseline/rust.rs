use std::thread;

fn main() {
    let value = 7;
    let child = thread::spawn(move || value * 2);
    assert_eq!(child.join().expect("child panicked"), 14);
}

use std::thread;

fn main() {
    let value = 7;
    let child = thread::spawn(|| value * 2);
    let _ = child.join();
}

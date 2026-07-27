struct Counter {
    ordinary: i32,
}

fn write(shared: &Counter) {
    shared.ordinary = 10;
}

fn main() {
    write(&Counter { ordinary: 9 });
}

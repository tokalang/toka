fn choose(value: bool) -> i32 {
    match value {
        true => 1,
        false => 0,
    }
}

fn main() {
    assert_eq!(choose(true), 1);
    assert_eq!(choose(false), 0);
}

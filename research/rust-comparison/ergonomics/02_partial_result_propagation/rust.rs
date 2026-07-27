struct Holder {
    result: Result<i32, &'static str>,
}

fn propagate(holder: Holder) -> Result<i32, &'static str> {
    Ok(holder.result?)
}

fn main() {
    assert_eq!(propagate(Holder { result: Ok(9) }), Ok(9));
    assert_eq!(propagate(Holder { result: Err("failed") }), Err("failed"));
}

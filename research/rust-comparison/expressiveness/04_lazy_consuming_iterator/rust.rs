// A lazy, consuming adapter that owns both the input iterator and mutable
// callback state.  It uses only Rust's standard Iterator protocol.

fn counted<I>(input: I) -> impl Iterator<Item = i32>
where
    I: Iterator<Item = i32>,
{
    let mut count = 0;
    input.map(move |x| {
        count += 1;
        x * count
    })
}

fn main() {
    let values: Vec<i32> = counted(vec![2, 2, 2].into_iter()).collect();
    assert_eq!(values, vec![2, 4, 6]);
}

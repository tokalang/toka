fn main() {
    let values = vec![100, 200];
    let sum: i32 = values.iter().copied().sum();
    assert_eq!(sum, 300);
    assert_eq!(values.len(), 2);
}

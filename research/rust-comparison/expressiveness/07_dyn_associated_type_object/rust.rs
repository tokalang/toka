trait Readable {
    type Item;
    fn read(&self) -> Self::Item;
}

struct IntBox(i32);

impl Readable for IntBox {
    type Item = i32;

    fn read(&self) -> Self::Item {
        self.0
    }
}

fn consume(item: &dyn Readable<Item = i32>) -> i32 {
    item.read()
}

fn main() {
    assert_eq!(consume(&IntBox(17)), 17);
}

// Rust can select a payload view (&mut Cell) or a handle-slot view
// (&mut Box<Cell>) at a call site.  A mutable Box view also dereferences to the
// payload, so it is not a direct source-level H-only capability.

struct Cell {
    value: i32,
}

fn overwrite_payload(cell: &mut Cell) {
    cell.value = 13;
}

fn replace_handle(slot: &mut Box<Cell>) {
    *slot = Box::new(Cell { value: 17 });
}

fn replace_and_overwrite(slot: &mut Box<Cell>) {
    slot.value = 19;
    *slot = Box::new(Cell { value: 23 });
}

fn main() {
    let mut payload_only = Box::new(Cell { value: 1 });
    overwrite_payload(&mut *payload_only);
    assert_eq!(payload_only.value, 13);

    let mut handle_only = Box::new(Cell { value: 2 });
    replace_handle(&mut handle_only);
    assert_eq!(handle_only.value, 17);

    let mut both = Box::new(Cell { value: 3 });
    replace_and_overwrite(&mut both);
    assert_eq!(both.value, 23);
}

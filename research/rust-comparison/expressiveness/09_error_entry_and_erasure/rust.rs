use std::error::Error;
use std::io;

fn read_value() -> Result<i32, io::Error> {
    Ok(23)
}

fn main() -> Result<(), Box<dyn Error>> {
    let value = read_value()?;
    assert_eq!(value, 23);
    Ok(())
}

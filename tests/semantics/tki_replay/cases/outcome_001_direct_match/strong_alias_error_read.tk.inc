import ./lib::{PacketAlias, BuildOutcome, try_build}
fn main() -> i32 {
    auto value = uninit:PacketAlias
    match try_build(init value, true) {
        auto BuildOutcome::Ok(_) => return 0
        auto BuildOutcome::Err(_) => return value.value
    }
}

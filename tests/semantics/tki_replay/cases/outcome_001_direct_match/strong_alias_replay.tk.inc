// Source and source-hidden strong-alias Outcome control for the dedicated gate.
import ./lib::{PacketAlias, BuildOutcome, try_build}
fn fill(init out: PacketAlias) { init out = PacketAlias(value = 7) }
fn main() -> i32 {
    auto direct = uninit:PacketAlias
    fill(init direct)
    assert(direct.value == 7:i32, "whole initialization updates tracked fields")
    auto value = uninit:PacketAlias
    match try_build(init value, false) {
        auto BuildOutcome::Ok(_) => return value.value - 42:i32
        auto BuildOutcome::Err(_) => return 1
    }
}

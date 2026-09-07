extern fn test_buffer_watch(*ptr: void) -> void
extern fn test_buffer_releases() -> i32

fn main() -> i32 {
    auto outputs = ModuleOutputs(interface = string::from("api.tki"),
        object = string::from("mod.o"), executable = string::from("app"))
    auto snapshot = ModuleSnapshot(outputs = outputs.clone(), ..)
    auto manifest# = DependencyManifest(..)
    manifest.modules#.insert(cede string::from("mod"), cede snapshot.clone())
    auto expected_outputs = "{\"interface\":\"api.tki\",\"object\":\"mod.o\",\"executable\":\"app\"}"
    auto expected_snapshot = "{\"kind\":\"\",\"fallback_triggered\":false,\"cache_status\":\"\",\"cache_status_reason\":\"\",\"target_triple\":\"\",\"compiler_version\":\"\",\"interface_version\":\"\",\"source_hash\":\"\",\"content_hash\":\"\",\"outputs\":{\"interface\":\"api.tki\",\"object\":\"mod.o\",\"executable\":\"app\"},\"dependencies\":[]}"
    {
        auto buf# = string::from("")
        buf#.ensure_capacity(16384:usize)
        auto capacity = buf.capacity()
        auto address = buf.c_str() as Addr
        test_buffer_watch(buf.c_str() as *void)
        buf = outputs_to_json(outputs, cede buf)
        assert(buf.equals(expected_outputs), "outputs JSON changed")
        assert(buf.capacity() == capacity, "outputs capacity changed")
        assert((buf.c_str() as Addr) == address, "outputs copied buffer")
        assert(test_buffer_releases() == 0, "outputs released live buffer")
    }
    assert(test_buffer_releases() == 1, "outputs buffer must free once")
    {
        auto buf# = string::from("")
        buf#.ensure_capacity(16384:usize)
        auto capacity = buf.capacity()
        auto address = buf.c_str() as Addr
        test_buffer_watch(buf.c_str() as *void)
        buf = snapshot_to_json(snapshot, cede buf)
        assert(buf.equals(expected_snapshot), "snapshot JSON changed")
        assert(buf.capacity() == capacity, "snapshot capacity changed")
        assert((buf.c_str() as Addr) == address, "snapshot copied buffer")
        assert(test_buffer_releases() == 0, "snapshot released live buffer")
    }
    assert(test_buffer_releases() == 1, "snapshot buffer must free once")
    {
        auto expected# = string::from("{\"manifest_version\":\"\",\"roots\":[],\"modules\":{\"mod\":")
        expected#.push_str(expected_snapshot)
        expected#.push_str("}}")
        auto buf# = string::from("")
        buf#.ensure_capacity(16384:usize)
        auto capacity = buf.capacity()
        auto address = buf.c_str() as Addr
        test_buffer_watch(buf.c_str() as *void)
        buf = manifest_to_json(manifest, cede buf)
        assert(buf.equals(expected.as_str()), "manifest JSON changed")
        assert(buf.capacity() == capacity, "manifest capacity changed")
        assert((buf.c_str() as Addr) == address, "manifest copied buffer")
        assert(test_buffer_releases() == 0, "manifest released live buffer")
    }
    assert(test_buffer_releases() == 1, "manifest buffer must free once")
    auto parsed_outputs# = ModuleOutputs(..)
    auto parsed_snapshot# = ModuleSnapshot(outputs = ModuleOutputs(..), ..)
    auto parsed_manifest# = DependencyManifest(..)
    auto parsed_info# = RebuildModuleInfo(..)
    auto parsed_plan# = RebuildPlan(..)
    assert(parsed_outputs#.parse_json("{}tail").unwrap().equals("tail"), "outputs parser remainder")
    assert(parsed_snapshot#.parse_json("{}tail").unwrap().equals("tail"), "snapshot parser remainder")
    assert(parsed_manifest#.parse_json("{}tail").unwrap().equals("tail"), "manifest parser remainder")
    assert(parsed_info#.parse_json("{}tail").unwrap().equals("tail"), "info parser remainder")
    assert(parsed_plan#.parse_json("{}tail").unwrap().equals("tail"), "plan parser remainder")
    assert(parsed_outputs#.parse_json("!").is_err(), "outputs parser failure")
    assert(parsed_snapshot#.parse_json("!").is_err(), "snapshot parser failure")
    assert(parsed_manifest#.parse_json("!").is_err(), "manifest parser failure")
    assert(parsed_info#.parse_json("!").is_err(), "info parser failure")
    assert(parsed_plan#.parse_json("!").is_err(), "plan parser failure")
    return 0
}

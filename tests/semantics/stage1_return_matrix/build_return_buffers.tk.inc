extern fn test_buffer_watch(*ptr: void) -> void
extern fn test_buffer_releases() -> i32
extern fn test_list_drop_reset() -> void
extern fn test_list_drop_events() -> i32
extern fn test_list_unfreed() -> i32

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

    // Nonempty metadata owns decoded text independently of the parser input.
    auto encoded# = string::from("{\"kind\":\"source\",\"dependencies\":[\"a:b\",\"\",\"line\\nend\",\"雪\",\"quote\\\"\\\\\"],\"outputs\":{\"object\":\"dir\\\\file.o\"}}tail")
    auto rich# = ModuleSnapshot(..)
    {
        auto status = rich#.parse_json(encoded.as_str())
        assert(status.unwrap().equals("tail"), "nonempty snapshot remainder")
    }
    encoded = string::from("input replaced")
    assert(rich.dependencies.len() == 5, "all dependencies parsed")
    assert(rich.dependencies.borrow(0).equals("a:b"), "framing delimiter preserved")
    assert(rich.dependencies.borrow(1).equals(""), "empty dependency preserved")
    assert(rich.dependencies.borrow(2).equals("line\nend"), "escaped newline decoded")
    assert(rich.dependencies.borrow(3).equals("雪"), "unicode dependency decoded")
    assert(rich.dependencies.borrow(4).equals("quote\"\\"), "escaped quote and slash decoded")
    assert(rich.outputs.object.equals("dir\\file.o"), "nested output decoded")
    auto rich_copy = rich.clone()
    auto json_buffer# = string::from("")
    auto serialized = snapshot_to_json(rich_copy, cede json_buffer)
    auto roundtrip# = ModuleSnapshot(..)
    assert(roundtrip#.parse_json(serialized.as_str()).is_ok(), "escaped output is valid JSON")
    assert(roundtrip.dependencies.borrow(4).equals("quote\"\\"), "snapshot JSON roundtrip")
    assert(roundtrip.outputs.object.equals("dir\\file.o"), "output JSON roundtrip")

    auto graph# = DependencyManifest(..)
    assert(graph#.parse_json("{\"manifest_version\":\"1.0\",\"roots\":[\"root\"],\"modules\":{\"root\":{\"kind\":\"source\",\"dependencies\":[\"leaf\"]},\"leaf\":{\"kind\":\"source\",\"dependencies\":[]}}}rest").unwrap().equals("rest"), "nonempty manifest parse")
    auto graph_copy = graph.clone()
    graph.modules#.insert(string::from("root"), ModuleSnapshot(..))
    {
        auto &root = graph_copy.modules.try_borrow(string::from("root")).unwrap()
        assert(root.dependencies.borrow(0).equals("leaf"), "clone storage independent of replacement")
    }
    auto dirty# = HashMap<string, string>::new()
    dirty#.insert(string::from("leaf"), string::from("changed"))
    auto memo# = HashMap<string, DfsResult>::new()
    auto visiting# = HashMap<string, bool>::new()
    auto chain = check_dirty_recursive(string::from("root"), graph_copy, dirty, memo#, visiting#)
    assert(chain.is_dirty && chain.reason.equals("dependency changed"), "transitive rebuild decision")
    assert(chain.chain.len() == 1 && chain.chain.borrow(0).equals("leaf"), "dependency chain contents")
    auto cached = check_dirty_recursive(string::from("root"), graph_copy, dirty, memo#, visiting#)
    assert(cached.chain.borrow(0).equals("leaf"), "memoized owned chain")
    auto plan# = RebuildPlan(..)
    assert(plan#.parse_json("{\"status\":\"dirty\",\"dirty_roots\":[\"root\"],\"dirty_modules\":{\"root\":{\"reason\":\"dependency changed\",\"dirty_deps\":[\"leaf\"]}}}tail").unwrap().equals("tail"), "nonempty rebuild plan")

    // A later parse failure must retire the temporary prefix exactly once,
    // without replacing the receiver. The test-only Drop observer is paired
    // with the actual string release instruction, not only a destructor count.
    auto preserved# = BuildStrings::new()
    preserved#.push(string::from("keep"))
    test_list_drop_reset()
    assert(preserved#.parse_json("[\"first\",false]").is_err(), "reject non-string element")
    assert(preserved.len() == 1 && preserved.borrow(0).equals("keep"), "failed parse retains old receiver")
    assert(test_list_drop_events() == 1 && test_list_unfreed() == 0, "partial prefix storage released once")
    assert(preserved#.parse_json("[\"first\",").is_err(), "reject truncated array")
    assert(test_list_drop_events() == 2 && test_list_unfreed() == 0, "truncated prefix storage released once")
    assert(preserved#.parse_json("[\"replacement\"]").is_ok(), "replace an initialized list")
    assert(preserved.borrow(0).equals("replacement"), "replacement installed")
    assert(test_list_unfreed() == 0, "old list storage released before return")
    return 0
}

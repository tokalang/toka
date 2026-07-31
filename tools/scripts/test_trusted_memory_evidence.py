#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys
import tempfile


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
TOKAC = os.path.abspath(os.environ.get(
    "TOKAC", os.path.join(ROOT, "build/bin/tokac")))
FLAG = "--experimental-memory-contracts=nocapture"

PROVIDER = """\
import core/traits::{@encap}

pub shape Payload(value: i32)

impl Payload@encap {
    fn drop(self#) {}
}

pub fn make_payload(value: i32) -> Payload {
    return Payload(value=value)
}

pub fn read_payload(data: Payload) -> i32 {
    return data.value
}

pub fn consume_payload(cede data: Payload) -> i32 {
    auto owned = cede data
    return owned.value
}
"""

PASS_CONSUMER = """\
import ./provider::{make_payload, read_payload, consume_payload}

pub fn main() -> i32 {
    auto payload = make_payload(42)
    auto observed = read_payload(payload)
    auto result = consume_payload(cede payload)
    if observed == 42 && result == 42 { return 0 }
    return 1
}
"""

FAIL_CONSUMER = """\
import ./provider::{make_payload, consume_payload}

pub fn main() -> i32 {
    auto payload = make_payload(42)
    return consume_payload(payload)
}
"""


def fnv1a(value):
    result = 14695981039346656037
    for byte in value.encode():
        result ^= byte
        result = (result * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % result


def write(path, content):
    with open(path, "w", encoding="utf-8") as stream:
        stream.write(content)


def environment(build_dir=None, use_cache=False):
    result = os.environ.copy()
    result.pop("TOKA_BUILD_DIR", None)
    result.pop("TOKA_USE_LIB_CACHE", None)
    if build_dir:
        result["TOKA_BUILD_DIR"] = build_dir
    if use_cache:
        result["TOKA_USE_LIB_CACHE"] = "1"
    return result


def invoke(arguments, env=None, expect_success=True):
    result = subprocess.run([TOKAC] + arguments, cwd=ROOT, env=env,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if expect_success and result.returncode != 0:
        raise AssertionError(result.stderr)
    if not expect_success and result.returncode == 0:
        raise AssertionError("expected compilation rejection")
    return result


def dependency_dump(consumer, build_dir=None, object_path=None,
                    use_cache=False):
    arguments = ["--dump-dependencies=json", consumer]
    if object_path:
        arguments.append(object_path)
    result = invoke(arguments, environment(build_dir, use_cache))
    return json.loads(result.stdout)


def provider_record(document):
    records = [record for path, record in document["modules"].items()
               if path.endswith(".tki") and
               record.get("memory_evidence_status") != "NotApplicable"]
    if len(records) != 1:
        raise AssertionError("expected one cached provider interface")
    return records[0]


def assert_language_result(consumer, output, env, object_path, accepted):
    arguments = ["-c", consumer, "-o", output]
    if object_path:
        arguments.append(object_path)
    result = invoke(arguments, env, expect_success=accepted)
    if not accepted and "cede" not in result.stderr:
        raise AssertionError("rejection lost cede obligation")


def restore(path, content):
    with open(path, "wb") as stream:
        stream.write(content)


def imported_summary(document, suffix):
    matches = [entry for entry in document["functions"]
               if entry["name"].endswith(suffix)]
    if len(matches) != 1:
        raise AssertionError("expected one summary for " + suffix)
    return matches[0]


def imported_contract(document, suffix):
    matches = [entry for entry in document["records"]
               if entry["function"].endswith(suffix) and
               entry["contract"] == "nocapture"]
    if len(matches) != 1:
        raise AssertionError("expected one nocapture record for " + suffix)
    return matches[0]


def main():
    if not os.path.isfile(TOKAC):
        raise AssertionError("tokac not found: " + TOKAC)
    with tempfile.TemporaryDirectory(
            prefix="toka_trusted_memory_evidence_") as work:
        provider = os.path.join(work, "provider.tk")
        pass_consumer = os.path.join(work, "pass_consumer.tk")
        fail_consumer = os.path.join(work, "fail_consumer.tk")
        write(provider, PROVIDER)
        write(pass_consumer, PASS_CONSUMER)
        write(fail_consumer, FAIL_CONSUMER)

        source_env = environment()
        assert_language_result(
            pass_consumer, os.path.join(work, "source-pass.o"),
            source_env, None, True)
        assert_language_result(
            fail_consumer, os.path.join(work, "source-fail.o"),
            source_env, None, False)
        source_document = dependency_dump(pass_consumer)
        if any(record.get("memory_evidence_status") != "NotApplicable"
               for record in source_document["modules"].values()):
            raise AssertionError("source compilation consumed cache evidence")

        build_dir = os.path.join(work, "build")
        os.makedirs(os.path.join(build_dir, "objects"))
        os.makedirs(os.path.join(build_dir, "interfaces"))
        stem = fnv1a(os.path.realpath(provider))
        object_path = os.path.join(build_dir, "objects", stem + ".o")
        interface_path = os.path.join(
            build_dir, "interfaces", stem + ".tki")
        evidence_path = os.path.join(
            build_dir, "interfaces", stem + ".tke")
        cache_env = environment(build_dir, True)
        invoke(["-c", provider, "-o", object_path], cache_env)
        if not all(os.path.isfile(path) for path in
                   (object_path, interface_path, evidence_path)):
            raise AssertionError("provider cache artifacts are incomplete")

        with open(evidence_path, "rb") as stream:
            first_evidence = stream.read()
        invoke(["-c", provider, "-o", object_path], cache_env)
        with open(evidence_path, "rb") as stream:
            if stream.read() != first_evidence:
                raise AssertionError("evidence sidecar is not deterministic")

        cache_document = dependency_dump(
            pass_consumer, build_dir, object_path, True)
        valid = provider_record(cache_document)
        if valid["memory_evidence_status"] != "Valid":
            raise AssertionError("build cache evidence was not validated")
        assert_language_result(
            pass_consumer, os.path.join(work, "cache-pass.o"),
            cache_env, object_path, True)
        assert_language_result(
            fail_consumer, os.path.join(work, "cache-fail.o"),
            cache_env, object_path, False)

        summary_dump = invoke([
            "--dump-memory-summaries=json", "-c", pass_consumer,
            object_path, "-o", os.path.join(work, "summary.o")],
            cache_env)
        summaries = json.loads(summary_dump.stdout)["functions"]
        imported = [entry for entry in summaries
                    if entry["name"].endswith("read_payload")]
        if len(imported) != 1 or imported[0]["origin"] != "signature_only":
            raise AssertionError("Phase 4B activated cached evidence early")

        experimental_summary_dump = invoke([
            FLAG, "--dump-memory-summaries=json", "-c", pass_consumer,
            object_path, "-o", os.path.join(work, "experimental-summary.o")],
            cache_env)
        experimental_summaries = json.loads(
            experimental_summary_dump.stdout)
        if imported_summary(
                experimental_summaries, "read_payload")["origin"] != \
                "trusted_cache":
            raise AssertionError("validated cache evidence was not activated")

        contract_dump = invoke([
            FLAG, "--dump-memory-contracts=json", "-c", pass_consumer,
            object_path, "-o", os.path.join(work, "contracts.o")], cache_env)
        contract = imported_contract(
            json.loads(contract_dump.stdout), "read_payload")
        if contract["decision"] != "Candidate" or \
                contract["reason"] != "ProvenByTrustedCache" or \
                not contract["emitted"]:
            raise AssertionError("trusted nocapture contract was not emitted")

        disabled_dump = invoke([
            FLAG, "--disable-borrow-check", "--dump-memory-contracts=json",
            "-c", pass_consumer, object_path, "-o",
            os.path.join(work, "disabled-contracts.o")], cache_env)
        disabled_contract = imported_contract(
            json.loads(disabled_dump.stdout), "read_payload")
        if disabled_contract["emitted"] or \
                disabled_contract["reason"] != "SignatureOnly":
            raise AssertionError("disabled PAL consumed trusted evidence")

        for level in ("-O0", "-O2", "-O3"):
            default_executable = os.path.join(
                work, "default-" + level[1:].lower())
            experimental_executable = os.path.join(
                work, "experimental-" + level[1:].lower())
            invoke([level, pass_consumer, object_path, "-o",
                    default_executable], cache_env)
            invoke([level, FLAG, pass_consumer, object_path, "-o",
                    experimental_executable], cache_env)
            default_run = subprocess.run(
                [default_executable], stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True)
            experimental_run = subprocess.run(
                [experimental_executable], stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True)
            if (default_run.returncode, default_run.stdout,
                    default_run.stderr) != \
                    (experimental_run.returncode, experimental_run.stdout,
                     experimental_run.stderr):
                raise AssertionError(level + " cache behavior differs")

        with open(object_path, "rb") as stream:
            pristine_object = stream.read()
        with open(evidence_path, "rb") as stream:
            pristine_evidence = stream.read()

        os.remove(evidence_path)
        missing = provider_record(dependency_dump(
            pass_consumer, build_dir, object_path, True))
        if missing["memory_evidence_status"] != "Missing":
            raise AssertionError("missing sidecar did not degrade")
        missing_contract_dump = invoke([
            FLAG, "--dump-memory-contracts=json", "-c", pass_consumer,
            object_path, "-o", os.path.join(work, "missing-contract.o")],
            cache_env)
        missing_contract = imported_contract(
            json.loads(missing_contract_dump.stdout), "read_payload")
        if missing_contract["emitted"] or \
                missing_contract["reason"] != "SignatureOnly":
            raise AssertionError("missing evidence affected nocapture")
        restore(evidence_path, pristine_evidence)

        write(evidence_path, "{")
        malformed = provider_record(dependency_dump(
            pass_consumer, build_dir, object_path, True))
        if malformed["memory_evidence_status"] != "ReadError":
            raise AssertionError("malformed sidecar did not degrade")
        restore(evidence_path, pristine_evidence)

        document = json.loads(pristine_evidence)
        document["version"] = 999
        write(evidence_path, json.dumps(document))
        schema = provider_record(dependency_dump(
            pass_consumer, build_dir, object_path, True))
        if schema["memory_evidence_status"] != "InvalidSchema":
            raise AssertionError("unsupported schema did not degrade")
        restore(evidence_path, pristine_evidence)

        for field in ("compiler_version", "interface_format",
                      "target_triple", "source_hash"):
            document = json.loads(pristine_evidence)
            document[field] = "invalid-" + field
            write(evidence_path, json.dumps(document))
            identity = provider_record(dependency_dump(
                pass_consumer, build_dir, object_path, True))
            if identity["memory_evidence_status"] != "IdentityMismatch":
                raise AssertionError(field + " mismatch did not degrade")
            restore(evidence_path, pristine_evidence)

        document = json.loads(pristine_evidence)
        document["functions"][0]["effects"] = 999
        write(evidence_path, json.dumps(document))
        record = provider_record(dependency_dump(
            pass_consumer, build_dir, object_path, True))
        if record["memory_evidence_status"] != "InvalidRecord":
            raise AssertionError("invalid record did not degrade")
        restore(evidence_path, pristine_evidence)

        document = json.loads(pristine_evidence)
        document["functions"][0]["effects"] ^= 1
        write(evidence_path, json.dumps(document))
        evidence = provider_record(dependency_dump(
            pass_consumer, build_dir, object_path, True))
        if evidence["memory_evidence_status"] != "EvidenceMismatch":
            raise AssertionError("valid-range evidence tamper did not degrade")
        restore(evidence_path, pristine_evidence)

        restore(object_path, pristine_object + b"tampered")
        obj = provider_record(dependency_dump(
            pass_consumer, build_dir, object_path, True))
        if obj["memory_evidence_status"] != "ObjectMismatch":
            raise AssertionError("object mismatch did not degrade")
        restore(object_path, pristine_object)

        standalone = os.path.join(work, "standalone")
        os.makedirs(standalone)
        standalone_tki = os.path.join(standalone, "provider.tki")
        standalone_tke = os.path.join(standalone, "provider.tke")
        standalone_object = os.path.join(standalone, "provider.o")
        shutil.copy2(interface_path, standalone_tki)
        shutil.copy2(evidence_path, standalone_tke)
        shutil.copy2(object_path, standalone_object)
        standalone_pass = os.path.join(standalone, "pass_consumer.tk")
        standalone_fail = os.path.join(standalone, "fail_consumer.tk")
        write(standalone_pass, PASS_CONSUMER)
        write(standalone_fail, FAIL_CONSUMER)
        os.rename(provider, provider + ".hidden")

        standalone_document = dependency_dump(
            standalone_pass, object_path=standalone_object)
        interface_records = [
            entry for path, entry in standalone_document["modules"].items()
            if path.endswith("provider.tki")]
        if len(interface_records) != 1 or \
                interface_records[0]["memory_evidence_status"] != \
                "NotApplicable":
            raise AssertionError("ordinary source-less TKI gained cache trust")
        standalone_env = environment()
        assert_language_result(
            standalone_pass, os.path.join(work, "standalone-pass.o"),
            standalone_env, standalone_object, True)
        assert_language_result(
            standalone_fail, os.path.join(work, "standalone-fail.o"),
            standalone_env, standalone_object, False)
        standalone_contract_dump = invoke([
            FLAG, "--dump-memory-contracts=json", "-c", standalone_pass,
            standalone_object, "-o",
            os.path.join(work, "standalone-contract.o")], standalone_env)
        standalone_contract = imported_contract(
            json.loads(standalone_contract_dump.stdout), "read_payload")
        if standalone_contract["emitted"] or \
                standalone_contract["reason"] != "SignatureOnly":
            raise AssertionError("ordinary TKI emitted trusted nocapture")

    print("Trusted memory evidence tests PASSED")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, KeyError, OSError, ValueError) as error:
        print("Trusted memory evidence tests FAILED: %s" % error,
              file=sys.stderr)
        sys.exit(1)

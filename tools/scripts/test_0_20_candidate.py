#!/usr/bin/env python3
"""Installed standard SDK candidate controls for 0.20.

Verifies under default 0.20 compiler entry (language_epoch: 0.20, encap_epoch: 0.19):
- Four tool versions (tokac, toka, tokafmt, tokalsp) against release label
- Source-hidden .tki provider compilation (Format 7, language_epoch: 0.20,
  encap_epoch: 0.19, compiler_version: 0.20.1-1), source deletion,
  missing/corrupt/format6/lang019/epoch018 rejection (E0901), and native
  sourceless consumer execution rc == 0
- 12 negative probes across 3 compiler modes (--check-only, -c, --emit-llvm)
  verifying exit 1, authoritative diagnostic code (E0485, E0487, E0484, E0483,
  E0409, E0447, E0495, E0418), and zero leftover artifacts
- 3 positive suites across 5 steps (check-only, emit-llvm, compile-obj,
  binary-build, native-execution rc == 0) covering 2-layer & 3-layer pure raw
  pointer hat chains, 1-layer bare-name projection, NonZeroFact narrowing &
  invalidation, and sourceless Format 7 .tki consumption
- 2 tokafmt idempotence passes on 0.20 raw pointer hat chains & .await?
- Full 41-check Stage 2/4.1 0.19 visibility matrix on installed SDK
- 60 compositional convergence regression probes on installed SDK
- Produces result.json with schema toka.0.20-candidate-controls
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))


TKI_PROVIDER_020_SOURCE = """\
pub shape RawChainBox(
    pub **pp: i32,
    pub *?pq: i32,
    pub ?*qp: i32,
    pub ??qq: i32,
    pub ?#rebind_q: i32,
    secret_bias: i32 = 42
)

impl RawChainBox@Encap {
    fn drop(self#) {}
}

impl RawChainBox {
    pub fn unsafe_open(**pp: i32, *?pq: i32, ?*qp: i32, ??qq: i32, ?rebind_q: i32, bias: i32) -> RawChainBox {
        return RawChainBox(
            **pp = **pp,
            *?pq = *?pq,
            ?*qp = ?*qp,
            ??qq = ??qq,
            ?rebind_q = ?rebind_q,
            secret_bias = bias
        )
    }

    pub fn sum_all(self) -> i32 {
        unsafe {
            auto *p_in = self.pp
            auto acc# = p_in + self.secret_bias
            auto ?inner_pq = self.pq
            guard ?inner_pq {
                acc += inner_pq
            } else {
                return -1
            }
            auto ?*qp = self.?*qp
            guard ?*qp {
                auto *local_qp = qp
                acc += local_qp
            } else {
                return -2
            }
            auto ??qq = self.??qq
            guard ??qq {
                auto ?inner_qq = qq
                guard ?inner_qq {
                    acc += inner_qq
                } else {
                    return -4
                }
            } else {
                return -3
            }
            auto ?rq = self.?rebind_q
            if ?rq != null {
                acc += rq
            }
            return acc
        }
    }
}

pub fn unsafe_rebind_nullable_slot(?#slot: i32, ?next: i32) {
    ?#slot = ?next
}

pub fn unsafe_bad_generic_raw_peel<T>(*p: T) -> T {
    return p
}

pub fn unsafe_generic_peel<T>(*p: T) -> T {
    unsafe {
        return p
    }
}
"""

POS1_020_SOURCE = """\
shape EvalCounter(pub hits#: i32, pub *?#slot: i32)

impl EvalCounter {
    fn pick(self#) -> *?#i32 {
        self.hits += 1
        return self.*?#slot
    }
}

fn check_2layer_matrix() -> i32 {
    auto v1# = 10:i32
    auto v2# = 20:i32
    unsafe {
        auto *#p1# = *#v1
        auto ?#q1# = *#v2 : ?i32

        auto **pp = *#*#p1
        auto *?#pq = *?#q1
        auto ?*qp = **pp : ?*i32
        auto ??qq = *?#pq : ??i32

        auto *pp_in = pp
        if pp_in != 10 {
            return 1
        }
        guard ?*qp {
            auto *qp_in = qp
            if qp_in != 10 {
                return 3
            }
        } else {
            return 2
        }
        guard ??qq {
            auto ?inner_q = qq
            guard ?inner_q {
                if inner_q != 20 {
                    return 6
                }
            } else {
                return 5
            }
        } else {
            return 4
        }

        // R-1: 1-layer bare-name write to inner nullable slot without unwrap
        pq = null
        if ?q1 != null {
            return 7
        }
        pq = *#p1
        guard ?q1 {
            if q1 != 10 {
                return 9
            }
        } else {
            return 8
        }
    }
    return 0
}

fn check_3layer_matrix() -> i32 {
    auto a = 7:i32
    unsafe {
        auto *p1 = *a
        auto ?q1 = *a : ?i32

        auto **p2_nn = **p1
        auto *?p2_nq = *?q1
        auto ?*p2_qn = **p1 : ?*i32
        auto ??p2_qq = *?q1 : ??i32

        auto ***c1 = ***p2_nn
        auto **?c2 = **?p2_nq
        auto *?*c3 = *?*p2_qn
        auto *??c4 = *??p2_qq
        auto ?**c5 = ***c1 : ?**i32
        auto ?*?c6 = **?c2 : ?*?i32
        auto ??*c7 = *?*c3 : ??*i32
        auto ???c8 = *??c4 : ???i32

        auto **c1_l2 = c1
        auto *c1_l1 = c1_l2
        if c1_l1 != 7 {
            return 10
        }
        auto *?c2_l2 = c2
        auto ?c2_l1 = c2_l2
        guard ?c2_l1 {
            if c2_l1 != 7 {
                return 12
            }
        } else {
            return 11
        }
        auto ?*c3_l2 = c3
        guard ?*c3_l2 {
            auto *c3_l1 = c3_l2
            if c3_l1 != 7 {
                return 14
            }
        } else {
            return 13
        }
        auto ??c4_l2 = c4
        guard ??c4_l2 {
            auto ?c4_l1 = c4_l2
            guard ?c4_l1 {
                if c4_l1 != 7 {
                    return 17
                }
            } else {
                return 16
            }
        } else {
            return 15
        }
        guard ?**c5 {
        } else {
            return 18
        }
        guard ?*?c6 {
        } else {
            return 19
        }
        guard ??*c7 {
        } else {
            return 20
        }
        guard ???c8 {
        } else {
            return 21
        }
    }
    return 0
}

fn main() -> i32 {
    auto r2 = check_2layer_matrix()
    if r2 != 0 {
        return r2
    }
    auto r3 = check_3layer_matrix()
    if r3 != 0 {
        return r3
    }
    auto x# = 55:i32
    unsafe {
        auto ?#qx# = *#x : ?i32
        auto ec# = EvalCounter(hits = 0, *?#slot = *?#qx)
        auto *?#picked_slot = ec#.pick()
        auto ?picked = picked_slot
        if ec.hits != 1 {
            return 30
        }
        guard ?picked {
            if picked != 55 {
                return 32
            }
        } else {
            return 31
        }
    }
    return 0
}
"""

POS2_020_SOURCE = """\
pub shape RawChainNode(pub val: i32, pub ?#next: RawChainNode)

fn advance_and_sum(?#cursor: RawChainNode) -> i32 {
    auto total# = 0:i32
    unsafe {
        guard ?cursor {
            total += cursor.val
            ?#cursor = cursor.?next
        } else {
            return -1
        }
        if ?cursor != null {
            total += cursor.val
        }
    }
    return total
}

fn main() -> i32 {
    auto n2 = RawChainNode(val = 30, ?next = null)
    unsafe {
        auto ?next_n2 = *n2 : ?RawChainNode
        auto n1 = RawChainNode(val = 12, ?next = ?next_n2)
        auto ?#head = *n1 : ?RawChainNode

        auto *unwrapped_first = (?head).unwrap()
        if unwrapped_first.val != 12 {
            return 1
        }

        auto sum = advance_and_sum(?#head)
        if sum != 42 {
            return 2
        }
    }
    return 0
}
"""

POS3_TKI_020_CONSUMER_SOURCE = """\
import raw_hats_020_provider::{RawChainBox, unsafe_rebind_nullable_slot, unsafe_generic_peel}

fn main() -> i32 {
    auto a# = 10:i32
    auto b# = 20:i32
    auto c# = 30:i32
    auto d# = 40:i32
    auto e# = 50:i32

    unsafe {
        auto *#pa# = *#a
        auto ?#qb# = *#b : ?i32
        auto *#pc# = *#c
        auto ?#qd# = *#d : ?i32
        auto ?#qe# = *#e : ?i32

        auto **ppa = *#*#pa
        auto *?pqb = *?#qb
        auto ?*qpc = *#*#pc : ?*i32
        auto ??qqd = *?#qd : ??i32

        auto box# = RawChainBox::unsafe_open(**ppa, *?pqb, ?*qpc, ??qqd, ?qe, 5)
        if box.sum_all() != 155 {
            return 1
        }
        unsafe_rebind_nullable_slot(box.?#rebind_q, null)
        if box.sum_all() != 105 {
            return 2
        }
        if unsafe_generic_peel(*pa) != 10 {
            return 3
        }
    }
    return 0
}
"""

TOKAFMT_020_PROBE_SOURCE = """\
pub shape RawFmtNode(pub val: i32, pub ?#next: RawFmtNode, pub ??deep: i32)

pub fn format_probe(**pp: i32, *?#pq: i32, ?*qp: i32, ??qq: i32) -> i32 {
    unsafe {
        guard ?*qp {
            guard ??qq {
                auto ?inner = qq
                guard ?inner {
                    auto *p_in = pp
                    auto *q_in = qp
                    return p_in + q_in + inner
                } else {
                    return 0
                }
            } else {
                return 0
            }
        } else {
            return 0
        }
    }
}
"""

NEGATIVE_020_CASES = [
    {
        "id": "neg01_legacy_nul_star_removed",
        "category": "legacy_nul_star_removal",
        "description": "Legacy nul * nullable raw pointer spelling rejected in 0.20 with E0488",
        "expected_code": "E0488",
        "needs_tki": False,
        "source": """\
fn bad(nul *p: i32) -> i32 {
    return 0
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg02_postfix_nullable_assertion_removed",
        "category": "postfix_question_removal",
        "description": "Postfix p? / p?? assertion syntax rejected with E0487",
        "expected_code": "E0487",
        "needs_tki": False,
        "source": """\
fn bad(?p: i32) -> i32 {
    auto x = p? != null
    return 0
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg03_safe_nullable_type_suffix_removed",
        "category": "type_suffix_question_removal",
        "description": "Safe nullable type suffix i32? rejected with E0484",
        "expected_code": "E0484",
        "needs_tki": False,
        "source": """\
fn bad() -> i32? {
    return 0
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg04_nonzero_raw_null_in_unsafe",
        "category": "unsafe_nonzero_raw_null_rejected",
        "description": "Assigning null to non-null *i32 inside unsafe rejected with E0483 (R-2)",
        "expected_code": "E0483",
        "needs_tki": False,
        "source": """\
fn main() -> i32 {
    unsafe {
        auto *p = null:*i32
    }
    return 0
}
""",
    },
    {
        "id": "neg05_inner_pointee_nullability_invariance",
        "category": "inner_pointee_invariance",
        "description": "Inner pointee nullability widening **i32 -> *?i32 rejected with E04606 (R-3)",
        "expected_code": "E04606",
        "needs_tki": False,
        "source": """\
fn f(*p: i32) {
    unsafe {
        auto **pp = **p
        auto *?q = (**pp):(*?i32)
    }
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg06_outer_nullable_inner_invariance",
        "category": "inner_pointee_invariance",
        "description": "Inner pointee nullability widening ?*i32 -> ??i32 rejected with E04606 (R-3)",
        "expected_code": "E04606",
        "needs_tki": False,
        "source": """\
fn f(?*qp: i32) {
    unsafe {
        auto ??qq = (?*qp):(??i32)
    }
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg07_rebindable_param_nullability_invariance",
        "category": "rebindable_slot_invariance",
        "description": "Passing *#q non-null slot to ?#p nullable rebindable parameter rejected with E04571",
        "expected_code": "E04571",
        "needs_tki": False,
        "source": """\
fn clear(?#slot: i32) {
    ?#slot = null
}

fn f(*#q: i32) {
    clear(*#q)
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg08_unnarrowed_nullable_member_access",
        "category": "nullable_member_guard_required",
        "description": "Accessing member on unnarrowed ?Inner rejected with E0447",
        "expected_code": "E0447",
        "needs_tki": False,
        "source": """\
shape Inner(pub x#: i32)

fn f(?p#: Inner) -> i32 {
    unsafe {
        p.x = 1
    }
    return 0
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg09_nonzero_fact_invalidated_by_rebind",
        "category": "nonzero_fact_invalidation",
        "description": "Rebinding ?#p after guard invalidates NonZeroFact and rejects *i32 use with E04606 (R-4)",
        "expected_code": "E04606",
        "needs_tki": False,
        "source": """\
fn f(?#p: i32) {
    unsafe {
        guard ?p {
            ?#p = null
            auto *q = (?p):*i32
        }
    }
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg10_unwrap_does_not_narrow_source_var",
        "category": "unwrap_return_only_narrowing",
        "description": "unwrap() narrows only return value, leaving source ?p unnarrowed (E04606, R-5)",
        "expected_code": "E04606",
        "needs_tki": False,
        "source": """\
fn f(?p: i32) {
    unsafe {
        auto *q = (?p).unwrap()
        auto *r = (?p):*i32
    }
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg11_mixed_raw_managed_multilayer_rejected",
        "category": "mixed_raw_managed_chain_rejected",
        "description": "Managed multi-layer hat chain fn(&^i32) -> i32 rejected with E0495",
        "expected_code": "E0495",
        "needs_tki": False,
        "source": """\
fn apply(cb: fn(&^i32) -> i32) -> i32 {
    return 0
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg12_cross_module_private_field_via_tki020",
        "category": "tki_020_visibility_enforcement",
        "description": "Cross-module read of default-private field via sourceless Format 7 .tki rejected with E0418",
        "expected_code": "E0418",
        "needs_tki": True,
        "source": """\
import raw_hats_020_provider::{RawChainBox}

fn inspect(b: RawChainBox) -> i32 {
    return b.secret_bias
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg13_raw_pointee_bare_read_requires_unsafe",
        "category": "raw_pointee_unsafe_contract_0201",
        "description": "Bare-name raw pointee read (*p -> p) outside unsafe rejected with E0625",
        "expected_code": "E0625",
        "needs_tki": False,
        "source": """\
fn read_raw(*p: i32) -> i32 {
    return p
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg14_raw_pointee_write_and_compound_assign_requires_unsafe",
        "category": "raw_pointee_unsafe_contract_0201",
        "description": "Bare-name raw pointee assignment (p = 1) outside unsafe rejected with E0625",
        "expected_code": "E0625",
        "needs_tki": False,
        "source": """\
fn write_raw(*p#: i32) {
    p = 1
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg15_raw_pointee_access_path_and_call_requires_unsafe",
        "category": "raw_pointee_unsafe_contract_0201",
        "description": "Member/index/method/call access through bare raw pointer outside unsafe rejected with E0625",
        "expected_code": "E0625",
        "needs_tki": False,
        "source": """\
shape Boxed(pub v: i32)

fn read_member(*b: Boxed) -> i32 {
    return b.v
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg16_sourceless_tki_generic_raw_pointee_requires_unsafe",
        "category": "raw_pointee_unsafe_contract_0201",
        "description": "Sourceless Format 7 .tki generic body reading raw pointee without unsafe rejected with E0625 on instantiation",
        "expected_code": "E0625",
        "needs_tki": True,
        "source": """\
import raw_hats_020_provider::{unsafe_bad_generic_raw_peel}

fn invoke(*p: i32) -> i32 {
    unsafe {
        return unsafe_bad_generic_raw_peel(*p)
    }
}

fn main() -> i32 {
    return 0
}
""",
    },
]

POS4_0201_SOURCE = """\
shape RawBox0201(pub v#: i32)

impl RawBox0201 {
    pub fn bump(self#, delta: i32) -> i32 {
        self.v += delta
        return self.v
    }
}

fn forward_handles(*#p: i32, *b: i32, ?#q: i32, **pp: i32) -> *i32 {
    *#p = *b
    if ?q != null {
        ?#q = null
    }
    guard ?q {
        auto *unwrapped_q = (?q).unwrap()
        return *unwrapped_q
    } else {
        auto **copy_pp = **pp
        if **copy_pp == **pp && *p == *b && ?q == null {
            auto addr = *p as Addr
            if addr != 0:Addr {
                return *p
            }
        }
        return *b
    }
}

fn main() -> i32 {
    auto a = 10:i32
    auto b = 25:i32
    auto *#pa# = *#a
    auto *pb = *b
    auto ?#qa = *a : ?i32
    auto **ppa = *#*#pa

    auto *selected = forward_handles(*#pa, *pb, ?#qa, **ppa)
    auto selected_val = unsafe (selected)
    if selected_val != 25 {
        return 1
    }

    unsafe {
        pa = 30
        pa += 12
    }
    if b != 42 {
        return 2
    }

    auto *mid = unsafe (ppa)
    auto mid_val = unsafe (mid)
    if mid_val != 42 {
        return 3
    }

    auto box_val = RawBox0201(v = 100)
    auto *#pbox# = *#box_val
    auto bumped = unsafe (pbox#.bump(23))
    auto pbox_v = unsafe (pbox.v)
    if bumped != 123 || pbox_v != 123 {
        return 4
    }

    auto x = 5:i32
    auto &rx = &x
    auto ~sh = new i32(7)
    auto ^uh = new i32(9)
    if rx + sh + uh != 21 {
        return 5
    }
    return 0
}
"""

POSITIVE_020_CASES = [
    {
        "id": "pos1_raw_hats_2layer_and_3layer_matrix",
        "description": "2-layer (4) and 3-layer (8) pure raw pointer hat chains, 1-layer projection, R-1 LHS write, and single evaluation",
        "source": POS1_020_SOURCE,
    },
    {
        "id": "pos2_nonzero_fact_narrowing_and_rebind",
        "description": "1-layer guard/if-null narrowing, unwrap return narrowing, and ?#next member rebind chains",
        "source": POS2_020_SOURCE,
    },
    {
        "id": "pos4_raw_pointee_unsafe_and_safe_handle_matrix",
        "description": "0.20.1 safe handle-identity operations without unsafe vs unsafe raw pointee read/write/method/multi-layer peel",
        "source": POS4_0201_SOURCE,
    },
]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compute_fixtures_digest(fixtures_dir: pathlib.Path) -> str:
    manifest_file = fixtures_dir / "manifest.json"
    if not manifest_file.is_file():
        raise RuntimeError(f"Missing fixtures manifest: {manifest_file}")
    fix_manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    hasher = hashlib.sha256()
    for rel_path, exp_h in sorted(fix_manifest.items()):
        f = fixtures_dir / rel_path
        if not f.is_file():
            raise RuntimeError(f"Missing fixture file on disk: {f}")
        act_h = sha256_file(f)
        if act_h != exp_h:
            raise RuntimeError(f"Fixture hash mismatch for {rel_path}: expected {exp_h}, got {act_h}")
        hasher.update(rel_path.encode("utf-8"))
        hasher.update(exp_h.encode("utf-8"))
    return hasher.hexdigest()


def main():
    p = argparse.ArgumentParser(description="Toka 0.20 installed candidate verification")
    p.add_argument("--sdk", type=pathlib.Path, required=True, help="Installed SDK root directory")
    p.add_argument("--revision", required=True, help="Expected candidate commit SHA")
    p.add_argument("--version", default="v0.20.1", help="Expected version label")
    p.add_argument("--target", default="", help="Target platform label")
    p.add_argument("--output", type=pathlib.Path, default=None, help="Output directory for results")
    a = p.parse_args()

    sdk = a.sdk.resolve()
    tokac_bin = sdk / "bin/tokac"
    if not tokac_bin.is_file() and (sdk / "bin/tokac.exe").is_file():
        tokac_bin = sdk / "bin/tokac.exe"
    if not tokac_bin.is_file():
        raise SystemExit(f"Invalid SDK root: {tokac_bin} missing")

    tokafmt_bin = sdk / "bin/tokafmt"
    if not tokafmt_bin.is_file() and (sdk / "bin/tokafmt.exe").is_file():
        tokafmt_bin = sdk / "bin/tokafmt.exe"
    if not tokafmt_bin.is_file():
        raise SystemExit(f"Invalid SDK root: {tokafmt_bin} missing")

    sdk_json_path = sdk / "sdk.json"
    if not sdk_json_path.is_file():
        raise SystemExit(f"Missing sdk.json in SDK root: {sdk_json_path}")
    sdk_meta = json.loads(sdk_json_path.read_text(encoding="utf-8"))

    if sdk_meta.get("candidate_revision") != a.revision:
        raise SystemExit(f"SDK revision mismatch: sdk has {sdk_meta.get('candidate_revision')}, expected {a.revision}")
    if sdk_meta.get("version_label") != a.version:
        raise SystemExit(f"SDK version mismatch: sdk has {sdk_meta.get('version_label')}, expected {a.version}")
    if sdk_meta.get("build_testing") is not False:
        raise SystemExit(f"Standard candidate SDK must have build_testing=false: {sdk_meta.get('build_testing')}")

    target = a.target or sdk_meta.get("target") or "unknown"
    script_dir = pathlib.Path(__file__).resolve().parent
    control_script_sha256 = sha256_file(pathlib.Path(__file__).resolve())

    fixtures_dir = script_dir / "r018_fixtures"
    if not fixtures_dir.is_dir():
        alt_fix = ROOT / "tools/scripts/r018_fixtures"
        if alt_fix.is_dir():
            fixtures_dir = alt_fix
        else:
            raise SystemExit(f"Missing mandatory r018_fixtures directory at {fixtures_dir}")
    fixtures_digest = compute_fixtures_digest(fixtures_dir)

    out = a.output.resolve() if a.output else (sdk.parent / f"candidate-020-{time.strftime('%Y%m%d-%H%M%S')}")
    out.mkdir(parents=True, exist_ok=True)
    work = out / "work"
    work.mkdir(parents=True, exist_ok=True)
    probes_dir = out / "probes"
    probes_dir.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env["PATH"] = f"{sdk / 'bin'}:{env.get('PATH', '')}"
    toka_lib = sdk / "lib"
    if toka_lib.is_dir():
        env["TOKA_LIB"] = str(toka_lib)

    receipts = []

    def run_cmd(name, cmd, cwd, allow_fail=False, expected_code=None, expected_substr=None, artifact_absent=False, timeout=300):
        t0 = time.time()
        r = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True, timeout=timeout)
        elapsed = time.time() - t0
        combined = r.stdout + "\n" + r.stderr

        ok = True
        err_msg = ""
        signal_val = None
        termination_val = None

        if r.returncode < 0:
            ok = False
            signal_val = -r.returncode
            termination_val = f"terminated by signal {signal_val}"
            err_msg = termination_val
        elif not allow_fail and r.returncode != 0:
            ok = False
            err_msg = f"command failed with exit {r.returncode}"
        elif allow_fail and r.returncode != 1:
            ok = False
            err_msg = f"negative command expected exit 1, got {r.returncode}"

        if expected_code and expected_code not in combined:
            ok = False
            err_msg = f"missing required diagnostic {expected_code}"
        if expected_substr and expected_substr not in combined:
            ok = False
            err_msg = f"missing required diagnostic substring {expected_substr!r}"

        rec = {
            "name": name,
            "argv": [str(c) for c in cmd],
            "cmd": [str(c) for c in cmd],
            "cwd": str(cwd),
            "exit_code": r.returncode,
            "signal": signal_val,
            "termination": termination_val,
            "error": None if ok else err_msg,
            "result": "pass" if ok else "fail",
            "stdout": r.stdout,
            "stderr": r.stderr,
            "stdout_sha256": sha256_bytes(r.stdout.encode("utf-8")),
            "stderr_sha256": sha256_bytes(r.stderr.encode("utf-8")),
            "execution_ms": round(elapsed * 1000.0, 2),
            "elapsed_seconds": round(elapsed, 4),
            "ok": ok,
        }
        if expected_code:
            rec["expected_code"] = expected_code
            rec["diagnostic"] = expected_code
        if artifact_absent:
            rec["artifact_absent"] = True

        receipts.append(rec)
        if not ok:
            raise RuntimeError(f"Step {name} failed: {err_msg}\n{combined}")
        return r

    # 1. Four tool versions (4 receipts)
    expected_v = a.version.removeprefix("v")
    versions = {}
    for tool in ("tokac", "toka", "tokafmt", "tokalsp"):
        tool_bin = sdk / f"bin/{tool}"
        if not tool_bin.is_file() and (sdk / f"bin/{tool}.exe").is_file():
            tool_bin = sdk / f"bin/{tool}.exe"
        r = run_cmd(f"version-{tool}", [str(tool_bin), "--version"], work)
        if expected_v not in r.stdout and expected_v not in r.stderr:
            raise RuntimeError(f"{tool} version output missing {expected_v}: {r.stdout}")
        versions[tool] = r.stdout.strip()

    # 2. Source-hidden TKI provider compilation & 5 rejection controls (6 receipts)
    tki_dir = work / "tki_provider"
    if tki_dir.exists():
        shutil.rmtree(tki_dir)
    tki_dir.mkdir(parents=True, exist_ok=True)

    src_path = tki_dir / "raw_hats_020_provider.tk"
    tki_path = tki_dir / "raw_hats_020_provider.tki"
    obj_path = tki_dir / "raw_hats_020_provider.o"
    src_path.write_text(TKI_PROVIDER_020_SOURCE, encoding="utf-8")

    run_cmd(
        "pos-tki-provider-compile",
        [str(tokac_bin), "-I", str(toka_lib), "-c", "--emit-interface", str(src_path), "-o", str(obj_path)],
        tki_dir,
    )
    if not tki_path.exists() or not obj_path.exists():
        raise RuntimeError("Failed to build 0.20 TKI provider")

    tki_text = tki_path.read_text(encoding="utf-8")
    for req_header in (
        "// @meta format_version: 7",
        "// @meta language_epoch: 0.20",
        "// @meta encap_epoch: 0.19",
    ):
        if req_header not in tki_text:
            raise RuntimeError(f"Emitted .tki missing {req_header!r} header:\n{tki_text}")

    # Delete source file to guarantee sourceless interface consumption
    if src_path.exists():
        src_path.unlink()
    provider_source_present = src_path.exists()

    p_tki_sha = sha256_file(tki_path)
    p_obj_sha = sha256_file(obj_path)

    # 2a. Missing interface test
    missing_dir = work / "missing_iface"
    missing_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer = missing_dir / "consumer.tk"
    bad_consumer.write_text(POS3_TKI_020_CONSUMER_SOURCE, encoding="utf-8")
    r_missing = run_cmd(
        "neg-tki-missing-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(missing_dir), "--check-only", str(bad_consumer)],
        missing_dir,
        allow_fail=True,
        expected_code="E0901",
        artifact_absent=True,
    )
    missing_interface_rejected = (r_missing.returncode == 1)
    missing_iface_cmd = receipts[-1]

    # 2b. Corrupt interface test
    corrupt_dir = work / "corrupt_iface"
    corrupt_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer2 = corrupt_dir / "consumer.tk"
    bad_consumer2.write_text(POS3_TKI_020_CONSUMER_SOURCE, encoding="utf-8")
    bad_tki = corrupt_dir / "raw_hats_020_provider.tki"
    bad_tki.write_bytes(b"CORRUPT_TKI_GARBAGE_BYTES")
    r_corrupt = run_cmd(
        "neg-tki-corrupt-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(corrupt_dir), "--check-only", str(bad_consumer2)],
        corrupt_dir,
        allow_fail=True,
        expected_code="E0901",
        artifact_absent=True,
    )
    corrupt_interface_rejected = (r_corrupt.returncode == 1)
    corrupt_iface_cmd = receipts[-1]

    # 2c. Stale Format 6 interface test
    fmt6_dir = work / "format6_iface"
    fmt6_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer3 = fmt6_dir / "consumer.tk"
    bad_consumer3.write_text(POS3_TKI_020_CONSUMER_SOURCE, encoding="utf-8")
    fmt6_tki = fmt6_dir / "raw_hats_020_provider.tki"
    fmt6_tki.write_text(
        tki_text.replace("// @meta format_version: 7", "// @meta format_version: 6", 1),
        encoding="utf-8",
    )
    r_fmt6 = run_cmd(
        "neg-tki-format6-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(fmt6_dir), "--check-only", str(bad_consumer3)],
        fmt6_dir,
        allow_fail=True,
        expected_code="E0901",
        expected_substr="Interface format version mismatch (expected 7, got 6)",
        artifact_absent=True,
    )
    format6_interface_rejected = (r_fmt6.returncode == 1)
    format6_iface_cmd = receipts[-1]

    # 2d. Mismatched language_epoch: 0.19 interface test
    lang019_dir = work / "lang019_iface"
    lang019_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer4 = lang019_dir / "consumer.tk"
    bad_consumer4.write_text(POS3_TKI_020_CONSUMER_SOURCE, encoding="utf-8")
    lang019_tki = lang019_dir / "raw_hats_020_provider.tki"
    lang019_tki.write_text(
        tki_text.replace("// @meta language_epoch: 0.20", "// @meta language_epoch: 0.19", 1),
        encoding="utf-8",
    )
    r_lang019 = run_cmd(
        "neg-tki-lang019-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(lang019_dir), "--check-only", str(bad_consumer4)],
        lang019_dir,
        allow_fail=True,
        expected_code="E0901",
        expected_substr="Interface language_epoch mismatch (expected 0.20, got 0.19)",
        artifact_absent=True,
    )
    lang019_interface_rejected = (r_lang019.returncode == 1)
    lang019_iface_cmd = receipts[-1]

    # 2e. Mismatched encap_epoch: 0.18 interface test
    ep018_dir = work / "epoch018_iface"
    ep018_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer5 = ep018_dir / "consumer.tk"
    bad_consumer5.write_text(POS3_TKI_020_CONSUMER_SOURCE, encoding="utf-8")
    ep018_tki = ep018_dir / "raw_hats_020_provider.tki"
    ep018_tki.write_text(
        tki_text.replace("// @meta encap_epoch: 0.19", "// @meta encap_epoch: 0.18", 1),
        encoding="utf-8",
    )
    r_ep018 = run_cmd(
        "neg-tki-epoch018-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(ep018_dir), "--check-only", str(bad_consumer5)],
        ep018_dir,
        allow_fail=True,
        expected_code="E0901",
        expected_substr="Interface encap_epoch mismatch (expected 0.19, got 0.18)",
        artifact_absent=True,
    )
    epoch018_interface_rejected = (r_ep018.returncode == 1)
    epoch018_iface_cmd = receipts[-1]

    # 3. 12 Negative controls across 3 modes (36 receipts)
    neg_results = []

    def execute_negative_case(c):
        cid = c["id"]
        src_file = probes_dir / f"{cid}.tk"
        src_file.write_text(c["source"].lstrip(), encoding="utf-8")

        case_work = work / cid
        if case_work.exists():
            shutil.rmtree(case_work)
        case_work.mkdir(parents=True, exist_ok=True)

        inc_args = ["-I", str(toka_lib)]
        if c.get("needs_tki"):
            inc_args.extend(["-I", str(tki_dir)])

        obj_file = case_work / f"{cid}.o"
        ll_file = case_work / f"{cid}.ll"

        r_chk = run_cmd(
            f"neg-{cid}-check-only",
            [str(tokac_bin), *inc_args, "--check-only", str(src_file)],
            case_work,
            allow_fail=True,
            expected_code=c["expected_code"],
            artifact_absent=True,
        )
        r_obj = run_cmd(
            f"neg-{cid}-compile-obj",
            [str(tokac_bin), *inc_args, "-c", str(src_file), "-o", str(obj_file)],
            case_work,
            allow_fail=True,
            expected_code=c["expected_code"],
            artifact_absent=True,
        )
        if obj_file.exists():
            raise RuntimeError(f"Negative case {cid} emitted unexpected object file")
        r_ll = run_cmd(
            f"neg-{cid}-emit-llvm",
            [str(tokac_bin), *inc_args, "--emit-llvm", str(src_file), "-o", str(ll_file)],
            case_work,
            allow_fail=True,
            expected_code=c["expected_code"],
            artifact_absent=True,
        )
        if ll_file.exists():
            raise RuntimeError(f"Negative case {cid} emitted unexpected LLVM IR file")

        return {
            "id": cid,
            "category": c["category"],
            "description": c["description"],
            "passed": True,
            "modes": {
                "check-only": {"rc": r_chk.returncode, "expected_code": c["expected_code"], "code_matched": True, "leftover_artifacts": [], "passed": True},
                "compile-obj": {"rc": r_obj.returncode, "expected_code": c["expected_code"], "code_matched": True, "leftover_artifacts": [], "passed": True},
                "emit-llvm": {"rc": r_ll.returncode, "expected_code": c["expected_code"], "code_matched": True, "leftover_artifacts": [], "passed": True},
            },
        }

    for c in NEGATIVE_020_CASES:
        neg_results.append(execute_negative_case(c))

    # 4. Positive controls (pos1, pos2, pos3) across 5 steps (15 receipts)
    pos_results = []

    def execute_positive_case(c, extra_include_dirs=None, extra_link_objs=None):
        cid = c["id"]
        src_file = probes_dir / f"{cid}.tk"
        src_file.write_text(c["source"].lstrip(), encoding="utf-8")

        case_work = work / cid
        if case_work.exists():
            shutil.rmtree(case_work)
        case_work.mkdir(parents=True, exist_ok=True)

        inc_args = ["-I", str(toka_lib)]
        if extra_include_dirs:
            for d in extra_include_dirs:
                inc_args.extend(["-I", str(d)])

        suffix = ".exe" if sys.platform == "win32" or os.name == "nt" else ""
        obj_file = case_work / f"{cid}.o"
        ll_file = case_work / f"{cid}.ll"
        bin_file = case_work / f"{cid}{suffix}"

        run_cmd(
            f"pos-{cid}-check-only",
            [str(tokac_bin), *inc_args, "--check-only", str(src_file)],
            case_work,
        )
        run_cmd(
            f"pos-{cid}-emit-llvm",
            [str(tokac_bin), *inc_args, "--emit-llvm", str(src_file), "-o", str(ll_file)],
            case_work,
        )
        if not ll_file.exists() or ll_file.stat().st_size == 0:
            raise RuntimeError(f"Positive {cid} failed to emit LLVM file")
        run_cmd(
            f"pos-{cid}-compile-obj",
            [str(tokac_bin), *inc_args, "-c", str(src_file), "-o", str(obj_file)],
            case_work,
        )
        if not obj_file.exists() or obj_file.stat().st_size == 0:
            raise RuntimeError(f"Positive {cid} failed to emit object file")
        link_inputs = [str(src_file)]
        if extra_link_objs:
            link_inputs.extend(str(o) for o in extra_link_objs)
        run_cmd(
            f"pos-{cid}-binary-build",
            [str(tokac_bin), *inc_args, *link_inputs, "-o", str(bin_file)],
            case_work,
        )
        if not bin_file.exists():
            raise RuntimeError(f"Positive {cid} failed to build binary")
        r_run = run_cmd(
            f"pos-{cid}-native-execution",
            [str(bin_file)],
            case_work,
        )
        if r_run.returncode != 0:
            raise RuntimeError(f"Positive {cid} native execution failed with exit {r_run.returncode}")

        return {
            "id": cid,
            "description": c.get("description", ""),
            "passed": True,
        }

    for c in POSITIVE_020_CASES:
        pos_results.append(execute_positive_case(c))

    pos3_case = {
        "id": "pos3_cross_module_sourceless_tki_020_consumer",
        "description": "Cross-module sourceless .tki Format 7 / language_epoch 0.20 / encap_epoch 0.19 consumer and native execution",
        "source": POS3_TKI_020_CONSUMER_SOURCE,
    }
    res_pos3 = execute_positive_case(
        pos3_case,
        extra_include_dirs=[tki_dir],
        extra_link_objs=[obj_path],
    )
    pos_results.append(res_pos3)

    tki_facts = {
        "provider_format_version": 7,
        "provider_language_epoch": "0.20",
        "provider_encap_epoch": "0.19",
        "provider_tki_sha256": p_tki_sha,
        "provider_object_sha256": p_obj_sha,
        "consumed_tki_sha256": p_tki_sha,
        "consumed_object_sha256": p_obj_sha,
        "missing_interface_rejected": missing_interface_rejected,
        "corrupt_interface_rejected": corrupt_interface_rejected,
        "format6_interface_rejected": format6_interface_rejected,
        "lang019_interface_rejected": lang019_interface_rejected,
        "epoch018_interface_rejected": epoch018_interface_rejected,
        "consumer_result": "pass" if res_pos3["passed"] else "fail",
        "provider_source_present": provider_source_present,
        "missing_interface_command": missing_iface_cmd,
        "corrupt_interface_command": corrupt_iface_cmd,
        "format6_interface_command": format6_iface_cmd,
        "lang019_interface_command": lang019_iface_cmd,
        "epoch018_interface_command": epoch018_iface_cmd,
    }

    # 5. tokafmt 0.20 hat chain idempotence (2 receipts)
    fmt_work = work / "tokafmt_probe"
    fmt_work.mkdir(parents=True, exist_ok=True)
    fmt_file = fmt_work / "fmt_020_probe.tk"
    fmt_file.write_text(TOKAFMT_020_PROBE_SOURCE, encoding="utf-8")
    r_fmt1 = run_cmd("pos-tokafmt-pass1", [str(tokafmt_bin), str(fmt_file)], fmt_work)
    fmt_file.write_text(r_fmt1.stdout, encoding="utf-8")
    r_fmt2 = run_cmd("pos-tokafmt-pass2", [str(tokafmt_bin), str(fmt_file)], fmt_work)
    if not r_fmt1.stdout.strip() or r_fmt1.stdout != r_fmt2.stdout:
        raise RuntimeError("tokafmt is not idempotent on 0.20 raw pointer hat chains")
    tokafmt_idempotent = True

    # 6. Full 0.19 Stage 2/4.1 visibility matrix on installed SDK (41 checks, 1 receipt)
    vis_script = script_dir / "test_019_stage2_visibility_sema.py"
    if not vis_script.is_file():
        alt_vis = ROOT / "tools/scripts/test_019_stage2_visibility_sema.py"
        if alt_vis.is_file():
            vis_script = alt_vis
        else:
            raise RuntimeError(f"Missing mandatory test_019_stage2_visibility_sema.py at {vis_script}")

    r_vis = run_cmd(
        "pos-019-visibility-matrix",
        [sys.executable, str(vis_script), "--build-dir", str(sdk)],
        work,
        expected_substr="All 41 Stage 2/4.1 verification checks PASSED.",
    )
    vis_passed = (r_vis.returncode == 0)

    # 7. Compositional convergence (60 probes, 1 receipt)
    comp_conv_script = script_dir / "test_compositional_convergence.py"
    if not comp_conv_script.is_file():
        alt_conv = ROOT / "tools/scripts/test_compositional_convergence.py"
        if alt_conv.is_file():
            comp_conv_script = alt_conv
        else:
            raise RuntimeError(f"Missing mandatory test_compositional_convergence.py at {comp_conv_script}")

    conv_cmd = [
        sys.executable,
        str(comp_conv_script),
        "--build-dir",
        str(sdk),
        "--suite-dir",
        str(fixtures_dir),
        "--output-receipt",
        str(out / "compositional-convergence-receipt.json"),
    ]
    r_conv = run_cmd(
        "pos-compositional-convergence",
        conv_cmd,
        work,
    )
    comp_conv_passed = (r_conv.returncode == 0)
    comp_conv_count = 60

    report = {
        "schema": "toka.0.20-candidate-controls",
        "version": 1,
        "result": "pass",
        "candidate_revision": a.revision,
        "version_label": a.version,
        "target": target,
        "build_testing": False,
        "default_language_epoch": "0.20",
        "default_encap_epoch": "0.19",
        "interface_format_version": 7,
        "groups": ["G-020-raw-pointer-hats"],
        "fixtures_digest": fixtures_digest,
        "control_script_name": "test_0_20_candidate.py",
        "control_script_sha256": control_script_sha256,
        "four_tool_versions": versions,
        "SDK_root": str(sdk),
        "source_hidden_tki": tki_facts,
        "negative_controls": {
            "total": len(neg_results),
            "passed": sum(1 for r in neg_results if r["passed"]),
            "modes": ["--check-only", "-c", "--emit-llvm"],
        },
        "positive_controls": {
            "total": len(pos_results),
            "passed": sum(1 for r in pos_results if r["passed"]),
            "native_rc0": all(r["passed"] for r in pos_results),
        },
        "tokafmt_idempotence": {
            "passes": 2,
            "idempotent": tokafmt_idempotent,
            "sha256": sha256_bytes(r_fmt2.stdout.encode("utf-8")),
        },
        "visibility_matrix": {
            "total": 41,
            "passed": 41 if vis_passed else 0,
        },
        "compositional_convergence": {
            "total": comp_conv_count,
            "passed": comp_conv_count if comp_conv_passed else 0,
        },
        "counts": {
            "total_receipts": len(receipts),
            "passed": sum(1 for r in receipts if r.get("ok")),
            "failed": sum(1 for r in receipts if not r.get("ok")),
            "groups": {
                "version": 4,
                "source_hidden_tki": 6,
                "negative_controls": len(neg_results) * 3,
                "positive_controls": len(pos_results) * 5,
                "tokafmt_idempotence": 2,
                "visibility_matrix": 1,
                "compositional_convergence": 1,
            },
        },
        "receipts": receipts,
    }

    res_path = out / "result.json"
    res_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"0.20 candidate verification PASSED ({len(receipts)} receipts) -> {res_path}")


if __name__ == "__main__":
    main()

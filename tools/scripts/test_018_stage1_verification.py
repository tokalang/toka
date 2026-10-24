#!/usr/bin/env python3
"""0.18 Stage 1 Verification Suite for cede self consuming static async trait methods.

Covers:
- 29 negative probes (26 source + 3 sourceless .tki) across 3 modes
  (--check-only, -c, --emit-llvm) verifying exit code 1, diagnostic match, and zero artifacts.
- 3 positive probes (including 7 lifecycle paths, borrowed/ref carriers, and cross-module sourceless .tki)
  across 3 compiler modes + native link and execution verifying rc == 0.
"""

import argparse
import datetime
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]

COMMON_PREAMBLE = """
import core/task::{TaskHandle}
import std/task::{block_on, sleep_async}
import core/result::{Result}

pub shape CloseError(pub code: i32)

pub trait @AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError>
}

pub fn close_any<T: @AsyncClose>(cede x: T) -> async Result<i32, CloseError> {
    auto owned = cede x
    return owned.close().await
}

pub shape BorrowedCarrier(
    pub inner: TaskHandle<i32>,
    pub tag: i32
)

impl BorrowedCarrier@AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError> {
        auto owned = cede self
        auto v = owned.inner.await
        return Result<i32, CloseError>::Ok(v + owned.tag)
    }
}

pub shape RefCarrier(
    pub &view: i32,
    pub token: i32
)

impl RefCarrier@AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError> {
        auto owned = cede self
        sleep_async(1:i32).await
        return Result<i32, CloseError>::Ok(owned.view + owned.token)
    }
}

pub shape Buffer(pub size: i32)
pub fn read_buf(b: Buffer) -> async i32 {
    return b.size
}
"""

NEGATIVE_CASES = [
    # Category 1: Forged same-name wrappers
    {
        "id": "neg1a_forged_block_on_noncede_start",
        "category": "Cat1_ForgedSameNameWrapper",
        "expected_code": "E0456",
        "description": "User-defined forged block_on(handle: TaskHandle<...>) without cede that detaches handle.start",
        "source": COMMON_PREAMBLE + """
fn block_on(handle: TaskHandle<Result<i32, CloseError>>) -> Result<i32, CloseError> <- handle {
    auto _s = handle.start
    return Result<i32, CloseError>::Ok(0:i32)
}

pub fn main() -> i32 {
    auto buf = Buffer(size = 42:i32)
    auto c = BorrowedCarrier(inner = read_buf(buf), tag = 1:i32)
    auto t = c.close()
    auto _r = block_on(t)
    return 0:i32
}
"""
    },
    {
        "id": "neg1b_forged_block_on_cede_start",
        "category": "Cat1_ForgedSameNameWrapper",
        "expected_code": "E0456",
        "description": "User-defined forged block_on(cede handle: TaskHandle<...>) inside consumer wrapper that detaches handle.start",
        "source": COMMON_PREAMBLE + """
fn block_on(cede handle: TaskHandle<Result<i32, CloseError>>) -> i32 {
    auto owned = cede handle
    auto _s = owned.start
    return 0:i32
}

fn launder_consumer<T: @AsyncClose>(cede x: T) -> i32 {
    auto owned = cede x
    auto t = owned.close()
    return block_on(cede t)
}

pub fn main() -> i32 {
    auto buf = Buffer(size = 42:i32)
    auto c = BorrowedCarrier(inner = read_buf(buf), tag = 1:i32)
    return launder_consumer(cede c)
}
"""
    },
    {
        "id": "neg1c_forged_spawn_scoped_wrapper",
        "category": "Cat1_ForgedSameNameWrapper",
        "expected_code": "E0456",
        "description": "User-defined forged spawn_scoped wrapper attempting to start a borrowed close() task",
        "source": COMMON_PREAMBLE + """
pub shape FakeScope(pub id: i32)

fn spawn_scoped(scope#: FakeScope, cede task: TaskHandle<Result<i32, CloseError>>) -> i32 {
    auto owned = cede task
    auto _s = owned.start
    return scope.id
}

pub fn main() -> i32 {
    auto buf = Buffer(size = 10:i32)
    auto scope# = FakeScope(id = 1:i32)
    auto c = BorrowedCarrier(inner = read_buf(buf), tag = 2:i32)
    auto t = c.close()
    return spawn_scoped(scope#, cede t)
}
"""
    },
    {
        "id": "neg1d_forged_close_any_sync_start",
        "category": "Cat1_ForgedSameNameWrapper",
        "expected_code": "E0456",
        "description": "Forged synchronous close_any wrapper detaching x.close().start on BorrowedCarrier",
        "source": COMMON_PREAMBLE + """
fn forged_close_sync<T: @AsyncClose>(cede x: T) -> i32 {
    auto owned = cede x
    auto t = owned.close()
    auto _s = t.start
    return 0:i32
}

pub fn main() -> i32 {
    auto buf = Buffer(size = 10:i32)
    auto c = BorrowedCarrier(inner = read_buf(buf), tag = 2:i32)
    return forged_close_sync(cede c)
}
"""
    },

    # Category 2: Untrusted cede self forwarding
    {
        "id": "neg2a_untrusted_cede_self_impl_detach",
        "category": "Cat2_UntrustedCedeSelfForwarding",
        "expected_code": "E0456",
        "description": "Untrusted @AsyncClose impl that detaches self.inner.start instead of awaiting, called on local-borrowing carrier",
        "source": COMMON_PREAMBLE + """
pub shape DetachCarrier(pub inner: TaskHandle<i32>)

impl DetachCarrier@AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError> {
        auto owned = cede self
        auto _s = owned.inner.start
        return Result<i32, CloseError>::Ok(0:i32)
    }
}

pub fn main() -> i32 {
    auto buf = Buffer(size = 99:i32)
    auto c = DetachCarrier(inner = read_buf(buf))
    auto r = block_on(c.close())
    match r {
        auto Result<i32, CloseError>::Ok(v) => { return v }
        auto Result<i32, CloseError>::Err(_) => { return 1:i32 }
    }
}
"""
    },
    {
        "id": "neg2b_untrusted_sync_forwarder_missing_dep",
        "category": "Cat2_UntrustedCedeSelfForwarding",
        "expected_code": "E0454",
        "description": "Sync helper forwarding x.close() without <- x dependency annotation on RefCarrier",
        "source": COMMON_PREAMBLE + """
fn bad_forward_ref(cede x: RefCarrier) -> TaskHandle<Result<i32, CloseError>> {
    auto owned = cede x
    auto t = owned.close()
    return cede t
}

pub fn main() -> i32 {
    return 0:i32
}
"""
    },
    {
        "id": "neg2c_untrusted_async_helper_detach",
        "category": "Cat2_UntrustedCedeSelfForwarding",
        "expected_code": "E0456",
        "description": "Async helper that detaches x.close().start instead of awaiting, called via sync consumer",
        "source": COMMON_PREAMBLE + """
fn bad_async_helper<T: @AsyncClose>(cede x: T) -> async Result<i32, CloseError> {
    auto owned = cede x
    auto t = owned.close()
    auto _s = t.start
    return Result<i32, CloseError>::Ok(0:i32)
}

fn sync_wrapper<T: @AsyncClose>(cede x: T) -> i32 {
    auto _r = block_on(bad_async_helper(cede x))
    return 0:i32
}

pub fn main() -> i32 {
    auto buf = Buffer(size = 7:i32)
    auto c = BorrowedCarrier(inner = read_buf(buf), tag = 1:i32)
    return sync_wrapper(cede c)
}
"""
    },
    {
        "id": "neg2d_sync_forwarder_local_borrow_escape",
        "category": "Cat2_UntrustedCedeSelfForwarding",
        "expected_code": "E0456",
        "description": "Sync helper with <- x returning x.close() TaskHandle instead of consuming it via block_on rejected at callsite on local-borrowing RefCarrier",
        "source": COMMON_PREAMBLE + """
fn forward_ref(cede x: RefCarrier) -> TaskHandle<Result<i32, CloseError>> <- x {
    auto owned = cede x
    auto t = owned.close()
    return cede t
}

pub fn leak_via_forwarder() -> TaskHandle<Result<i32, CloseError>> {
    auto local_val = 42:i32
    auto r = RefCarrier(&view = &local_val, token = 5:i32)
    auto t = forward_ref(cede r)
    return cede t
}

pub fn main() -> i32 {
    return 0:i32
}
"""
    },

    # Category 3: Returning cede self carrying local borrow escape
    {
        "id": "neg3a_concrete_cede_self_return_borrowed_carrier",
        "category": "Cat3_ReturnLocalBorrowEscape",
        "expected_code": "E0455",
        "description": "Returning TaskHandle from concrete c.close() where c borrows a local variable",
        "source": COMMON_PREAMBLE + """
pub fn leak_concrete() -> TaskHandle<Result<i32, CloseError>> {
    auto buf = Buffer(size = 42:i32)
    auto c = BorrowedCarrier(inner = read_buf(buf), tag = 1:i32)
    auto t = c.close()
    return cede t
}

pub fn main() -> i32 {
    return 0:i32
}
"""
    },
    {
        "id": "neg3b_generic_async_close_return_borrowed_carrier",
        "category": "Cat3_ReturnLocalBorrowEscape",
        "expected_code": "E0455",
        "description": "Returning TaskHandle from generic close_any(cede c) where c borrows a local variable",
        "source": COMMON_PREAMBLE + """
pub fn leak_generic() -> TaskHandle<Result<i32, CloseError>> {
    auto buf = Buffer(size = 42:i32)
    auto c = BorrowedCarrier(inner = read_buf(buf), tag = 1:i32)
    auto t = close_any(cede c)
    return cede t
}

pub fn main() -> i32 {
    return 0:i32
}
"""
    },
    {
        "id": "neg3c_concrete_cede_self_start_borrowed_carrier",
        "category": "Cat3_ReturnLocalBorrowEscape",
        "expected_code": "E04584",
        "description": "Detaching (c.close()).start where c borrows a local variable",
        "source": COMMON_PREAMBLE + """
pub fn main() -> i32 {
    auto buf = Buffer(size = 42:i32)
    auto c = BorrowedCarrier(inner = read_buf(buf), tag = 1:i32)
    auto t = c.close()
    auto _s = t.start
    return 0:i32
}
"""
    },
    {
        "id": "neg3d_generic_async_close_start_borrowed_carrier",
        "category": "Cat3_ReturnLocalBorrowEscape",
        "expected_code": "E04584",
        "description": "Detaching (close_any(cede c)).start where c borrows a local variable",
        "source": COMMON_PREAMBLE + """
pub fn main() -> i32 {
    auto buf = Buffer(size = 42:i32)
    auto c = BorrowedCarrier(inner = read_buf(buf), tag = 1:i32)
    auto t = close_any(cede c)
    auto _s = t.start
    return 0:i32
}
"""
    },

    # Category 4: &T composite carrier (RefCarrier) escape paths
    {
        "id": "neg4a_ref_carrier_concrete_close_return",
        "category": "Cat4_RefCarrierEscape",
        "expected_code": "E0455",
        "description": "Returning TaskHandle from concrete r.close() on RefCarrier(&view: i32, token: i32) borrowing local",
        "source": COMMON_PREAMBLE + """
pub fn leak_ref_concrete() -> TaskHandle<Result<i32, CloseError>> {
    auto local_val = 42:i32
    auto r = RefCarrier(&view = &local_val, token = 5:i32)
    auto t = r.close()
    return cede t
}

pub fn main() -> i32 {
    return 0:i32
}
"""
    },
    {
        "id": "neg4b_ref_carrier_generic_close_return",
        "category": "Cat4_RefCarrierEscape",
        "expected_code": "E0455",
        "description": "Returning TaskHandle from generic close_any(cede r) on RefCarrier(&view: i32, token: i32) borrowing local",
        "source": COMMON_PREAMBLE + """
pub fn leak_ref_generic() -> TaskHandle<Result<i32, CloseError>> {
    auto local_val = 42:i32
    auto r = RefCarrier(&view = &local_val, token = 5:i32)
    auto t = close_any(cede r)
    return cede t
}

pub fn main() -> i32 {
    return 0:i32
}
"""
    },
    {
        "id": "neg4c_ref_carrier_concrete_close_start",
        "category": "Cat4_RefCarrierEscape",
        "expected_code": "E04584",
        "description": "Detaching (r.close()).start on RefCarrier(&view: i32, token: i32) borrowing local",
        "source": COMMON_PREAMBLE + """
pub fn main() -> i32 {
    auto local_val = 42:i32
    auto r = RefCarrier(&view = &local_val, token = 5:i32)
    auto t = r.close()
    auto _s = t.start
    return 0:i32
}
"""
    },
    {
        "id": "neg4d_ref_carrier_generic_close_start",
        "category": "Cat4_RefCarrierEscape",
        "expected_code": "E04584",
        "description": "Detaching (close_any(cede r)).start on RefCarrier(&view: i32, token: i32) borrowing local",
        "source": COMMON_PREAMBLE + """
pub fn main() -> i32 {
    auto local_val = 42:i32
    auto r = RefCarrier(&view = &local_val, token = 5:i32)
    auto t = close_any(cede r)
    auto _s = t.start
    return 0:i32
}
"""
    },

    # Category 6: Unrelaxed forms still rejected
    {
        "id": "neg6a_unrelaxed_cede_self_mut",
        "category": "Cat6_UnrelaxedFormsRejected",
        "expected_code": "E0618",
        "description": "Trait async method with 'cede self#' must still be rejected with E0618",
        "source": """
trait @BadAsyncClose {
    pub fn close(cede self#) -> async i32
}
fn main() -> i32 { return 0:i32 }
"""
    },
    {
        "id": "neg6b_unrelaxed_init_cede_self",
        "category": "Cat6_UnrelaxedFormsRejected",
        "expected_code": "E0618",
        "description": "Trait async method with 'init cede self' must still be rejected with E0618",
        "source": """
trait @BadAsyncClose {
    pub fn close(init cede self) -> async i32
}
fn main() -> i32 { return 0:i32 }
"""
    },
    {
        "id": "neg6c_unrelaxed_plain_self",
        "category": "Cat6_UnrelaxedFormsRejected",
        "expected_code": "E0618",
        "description": "Trait async method with plain 'self' must still be rejected with E0618",
        "source": """
trait @BadAsyncClose {
    pub fn close(self) -> async i32
}
fn main() -> i32 { return 0:i32 }
"""
    },
    {
        "id": "neg6d_unrelaxed_ref_self",
        "category": "Cat6_UnrelaxedFormsRejected",
        "expected_code": "E0618",
        "description": "Trait async method with 'self: &Self' must still be rejected with E0618",
        "source": """
trait @BadAsyncClose {
    pub fn close(self: &Self) -> async i32
}
fn main() -> i32 { return 0:i32 }
"""
    },
    {
        "id": "neg6e_unrelaxed_unique_self",
        "category": "Cat6_UnrelaxedFormsRejected",
        "expected_code": "E0618",
        "description": "Trait async method with 'self: ~Self' must still be rejected with E0618",
        "source": """
trait @BadAsyncClose {
    pub fn close(self: ~Self) -> async i32
}
fn main() -> i32 { return 0:i32 }
"""
    },
    {
        "id": "neg6f_unrelaxed_static_no_self",
        "category": "Cat6_UnrelaxedFormsRejected",
        "expected_code": "E0618",
        "description": "Static trait async method without self must still be rejected with E0618",
        "source": """
trait @BadAsyncClose {
    pub fn close(x: i32) -> async i32
}
fn main() -> i32 { return 0:i32 }
"""
    },
    {
        "id": "neg6g_unrelaxed_dyn_async_close",
        "category": "Cat6_UnrelaxedFormsRejected",
        "expected_code": "E0617",
        "description": "dyn @AsyncClose on a cede self async trait must still be rejected with E0617",
        "source": """
trait @AsyncClose {
    pub fn close(cede self) -> async i32
}
fn use_dyn(&x: dyn @AsyncClose) -> i32 {
    return 0:i32
}
fn main() -> i32 { return 0:i32 }
"""
    },
    {
        "id": "neg6h_mismatch_trait_cede_self_impl_self_mut",
        "category": "Cat6_UnrelaxedFormsRejected",
        "expected_code": "E0428",
        "description": "Trait declares 'cede self' but impl declares 'self#' -> rejected with E0428",
        "source": """
trait @AsyncClose {
    pub fn close(cede self) -> async i32
}
shape S(v: i32)
impl S@AsyncClose {
    pub fn close(self#) -> async i32 {
        return self.v
    }
}
fn main() -> i32 { return 0:i32 }
"""
    },
    {
        "id": "neg6i_mismatch_trait_self_mut_impl_cede_self",
        "category": "Cat6_UnrelaxedFormsRejected",
        "expected_code": "E0428",
        "description": "Trait declares 'self#' but impl declares 'cede self' -> rejected with E0428",
        "source": """
trait @AsyncStep {
    pub fn step(self#) -> async i32
}
shape S(v: i32)
impl S@AsyncStep {
    pub fn step(cede self) -> async i32 {
        auto owned = cede self
        return owned.v
    }
}
fn main() -> i32 { return 0:i32 }
"""
    },
    {
        "id": "neg6j_mismatch_trait_cede_self_impl_init_cede_self",
        "category": "Cat6_UnrelaxedFormsRejected",
        "expected_code": "E0428",
        "description": "Trait declares 'cede self' but impl declares 'init cede self' -> rejected with E0428",
        "source": """
trait @AsyncClose {
    pub fn close(cede self) -> async i32
}
shape S(v: i32)
impl S@AsyncClose {
    pub fn close(init cede self) -> async i32 {
        return self.v
    }
}
fn main() -> i32 { return 0:i32 }
"""
    },
]

# Category 5: Cross-module / TKI negative cases
TKI_PROVIDER_SOURCE = """
import core/task::{TaskHandle}
import std/task::{sleep_async}
import core/result::{Result}

pub shape CloseError(pub code: i32)

pub trait @AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError>
}

pub fn close_any<T: @AsyncClose>(cede x: T) -> async Result<i32, CloseError> {
    auto owned = cede x
    return owned.close().await
}

pub shape RefCarrier(
    pub &view: i32,
    pub token: i32
)

impl RefCarrier@AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError> {
        auto owned = cede self
        sleep_async(1:i32).await
        return Result<i32, CloseError>::Ok(owned.view + owned.token)
    }
}
"""

def make_bodyless_tki_content() -> str:
    decls = (
        "import tki_close_provider :: {CloseError}\n"
        "import core/task :: {TaskHandle}\n"
        "import core/result :: {Result}\n\n"
        "pub fn bodyless_sync_consume(cede h: TaskHandle<Result<i32, CloseError>>) -> i32\n"
    )
    h = 14695981039346656037
    for b in ("// @language_epoch 0.20\n// @encap_epoch 0.19\n" + decls).encode("utf-8"):
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    surface_hash = f"{h:016x}"
    header = (
        "// @meta compiler_version: any\n"
        "// @meta format_version: 7\n"
        "// @meta language_epoch: 0.20\n"
        "// @meta encap_epoch: 0.19\n"
        f"// @meta replay_surface_hash: {surface_hash}\n"
        "// @meta target_triple: any\n"
        "// @meta source_hash: any\n"
        "// @meta identity_schema_version: 2\n"
        "// @meta place_yield_abi_schema: 1\n"
        "// @meta logical_module_path: unbound\n"
        "// @meta resolver_binding_digest: unbound\n\n"
    )
    return header + decls

BODYLESS_TKI_CONTENT = make_bodyless_tki_content()

TKI_NEGATIVE_CASES = [
    {
        "id": "neg5a_cross_module_tki_bodyless_consumer_borrowed_task",
        "category": "Cat5_CrossModuleTkiEscape",
        "expected_code": "E0456",
        "description": "Cross-module sourceless .tki bodyless function consuming a local-borrowing close() TaskHandle",
        "source": """
import tki_close_provider::{CloseError, @AsyncClose, RefCarrier}
import bodyless_close_consumer::{bodyless_sync_consume}
import core/task::{TaskHandle}
import core/result::{Result}

pub fn main() -> i32 {
    auto local_val = 42:i32
    auto r = RefCarrier(&view = &local_val, token = 1:i32)
    auto t = r.close()
    return bodyless_sync_consume(cede t)
}
"""
    },
    {
        "id": "neg5b_cross_module_tki_ref_carrier_return_escape",
        "category": "Cat5_CrossModuleTkiEscape",
        "expected_code": "E0455",
        "description": "Cross-module sourceless .tki RefCarrier @AsyncClose returning TaskHandle carrying local borrow",
        "source": """
import tki_close_provider::{CloseError, @AsyncClose, RefCarrier, close_any}
import core/task::{TaskHandle}
import core/result::{Result}

pub fn leak_cross_module() -> TaskHandle<Result<i32, CloseError>> {
    auto local_val = 42:i32
    auto r = RefCarrier(&view = &local_val, token = 1:i32)
    auto t = close_any(cede r)
    return cede t
}

pub fn main() -> i32 {
    return 0:i32
}
"""
    },
    {
        "id": "neg5c_cross_module_tki_ref_carrier_start_escape",
        "category": "Cat5_CrossModuleTkiEscape",
        "expected_code": "E04584",
        "description": "Cross-module sourceless .tki RefCarrier @AsyncClose detaching .start with local borrow",
        "source": """
import tki_close_provider::{CloseError, @AsyncClose, RefCarrier, close_any}
import core/task::{TaskHandle}
import core/result::{Result}

pub fn main() -> i32 {
    auto local_val = 42:i32
    auto r = RefCarrier(&view = &local_val, token = 1:i32)
    auto t = close_any(cede r)
    auto _s = t.start
    return 0:i32
}
"""
    },
]

POSITIVE_CASES = [
    {
        "id": "pos1_async_close_7_lifecycle_paths",
        "description": "All 7 lifecycle paths for trait @AsyncClose { fn close(cede self) -> async Result<i32, CloseError> } including cold drop without outer @Encap, caught cancellation, and uncaught cancellation",
        "source": """
import core/task::{TaskHandle, task_get_coro_handle, toka_task_start}
import std/task::{block_on, sleep_async, task_cancel}
import std/async::{race2, RaceWinner}
import core/result::{Result}
import core/option::{Option}

extern fn toka_rt_live_wait_registry_count() -> u32

auto g_live# = 0:i32
auto g_drops# = 0:i32

pub shape CloseError(pub code: i32)

pub trait @AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError>
}

pub fn close_any<T: @AsyncClose>(cede x: T) -> async Result<i32, CloseError> {
    auto owned = cede x
    return owned.close().await
}

pub shape TrackedGuard(pub id: i32)
impl TrackedGuard@Encap {
    fn drop(self#) {
        g_live -= 1:i32
        g_drops += 1:i32
    }
}

fn make_guard(id: i32) -> TrackedGuard {
    g_live += 1:i32
    return TrackedGuard(id = id)
}

fn fail_if(flag: bool, code: i32) -> Result<i32, CloseError> {
    if flag {
        return Result<i32, CloseError>::Err(CloseError(code = code))
    }
    return Result<i32, CloseError>::Ok(1:i32)
}

// Composite carrier WITHOUT outer @Encap (relies on compiler-generated field glue for TrackedGuard)
pub shape SessionA(
    pub res_guard: TrackedGuard,
    pub mode: i32,
    pub base: i32
)

impl SessionA@AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError> {
        auto owned = cede self
        // mode == 1: pre-suspend error via !
        auto _ok1 = fail_if(owned.mode == 1:i32, 101:i32)!
        if owned.mode == 3:i32 {
            // Caught cancellation inside close(cede self) via .await?
            auto waited = sleep_async(500:i32).await?
            if waited.is_none() {
                return Result<i32, CloseError>::Err(CloseError(code = 303:i32))
            }
            return Result<i32, CloseError>::Ok(owned.base)
        }
        if owned.mode == 4:i32 {
            // Uncaught cancellation inside close(cede self)
            sleep_async(80:i32).await
            return Result<i32, CloseError>::Ok(owned.base)
        }
        sleep_async(1:i32).await
        // mode == 2: post-suspend error via !
        auto _ok2 = fail_if(owned.mode == 2:i32, 202:i32)!
        return Result<i32, CloseError>::Ok(owned.base + owned.res_guard.id)
    }
}

// Second concrete type implementing @AsyncClose
pub shape SessionB(
    pub g1: TrackedGuard,
    pub g2: TrackedGuard,
    pub bonus: i32
)

impl SessionB@AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError> {
        auto owned = cede self
        sleep_async(1:i32).await
        return Result<i32, CloseError>::Ok(owned.g1.id + owned.g2.id + owned.bonus)
    }
}

fn fast_cancel_winner() -> async Result<i32, CloseError> {
    sleep_async(5:i32).await
    return Result<i32, CloseError>::Ok(999:i32)
}

fn run_all_7_paths() -> async i32 {
    // Path 1: Normal completion (concrete & generic) -> 2 guards
    {
        auto s1 = SessionA(res_guard = make_guard(10:i32), mode = 0:i32, base = 32:i32)
        auto r1 = s1.close().await
        match r1 {
            auto Result<i32, CloseError>::Ok(v) => { if v != 42:i32 { return 11:i32 } }
            auto Result<i32, CloseError>::Err(_) => { return 12:i32 }
        }
        if g_live != 0:i32 { return 13:i32 }

        auto s1b = SessionA(res_guard = make_guard(20:i32), mode = 0:i32, base = 22:i32)
        auto r1b = close_any(cede s1b).await
        match r1b {
            auto Result<i32, CloseError>::Ok(v) => { if v != 42:i32 { return 14:i32 } }
            auto Result<i32, CloseError>::Err(_) => { return 15:i32 }
        }
        if g_live != 0:i32 { return 16:i32 }
    }

    // Path 2: Second concrete type implementing @AsyncClose via generic close_any -> 2 guards
    {
        auto s2 = SessionB(g1 = make_guard(10:i32), g2 = make_guard(15:i32), bonus = 17:i32)
        auto r2 = close_any(cede s2).await
        match r2 {
            auto Result<i32, CloseError>::Ok(v) => { if v != 42:i32 { return 21:i32 } }
            auto Result<i32, CloseError>::Err(_) => { return 22:i32 }
        }
        if g_live != 0:i32 { return 23:i32 }
    }

    // Path 3: Pre-suspend early error return via ! -> 1 guard
    {
        auto s3 = SessionA(res_guard = make_guard(30:i32), mode = 1:i32, base = 0:i32)
        auto r3 = close_any(cede s3).await
        match r3 {
            auto Result<i32, CloseError>::Ok(_) => { return 31:i32 }
            auto Result<i32, CloseError>::Err(e) => { if e.code != 101:i32 { return 32:i32 } }
        }
        if g_live != 0:i32 { return 33:i32 }
    }

    // Path 4: Post-suspend error return via ! -> 1 guard
    {
        auto s4 = SessionA(res_guard = make_guard(40:i32), mode = 2:i32, base = 0:i32)
        auto r4 = close_any(cede s4).await
        match r4 {
            auto Result<i32, CloseError>::Ok(_) => { return 41:i32 }
            auto Result<i32, CloseError>::Err(e) => { if e.code != 202:i32 { return 42:i32 } }
        }
        if g_live != 0:i32 { return 43:i32 }
    }

    // Path 5: Cold TaskHandle drop on composite struct WITHOUT outer @Encap -> 3 guards
    {
        {
            auto s5 = SessionA(res_guard = make_guard(50:i32), mode = 0:i32, base = 1:i32)
            auto _cold_handle = s5.close()
        }
        if g_live != 0:i32 { return 51:i32 }
        {
            auto s5b = SessionB(g1 = make_guard(51:i32), g2 = make_guard(52:i32), bonus = 2:i32)
            auto _cold_generic = close_any(cede s5b)
        }
        if g_live != 0:i32 { return 52:i32 }
    }

    // Path 6: Caught cancellation inside close(cede self) -> 1 guard
    {
        auto s6 = SessionA(res_guard = make_guard(60:i32), mode = 3:i32, base = 77:i32)
        auto task6 = s6.close()
        auto ?tcb6 = task_get_coro_handle<Result<i32, CloseError>>(task6)
        auto *live_tcb6 = tcb6.unwrap()
        toka_task_start(*live_tcb6)
        auto waited6# = 0:i32
        loop toka_rt_live_wait_registry_count() == 0:u32 && waited6 < 100:i32 {
            sleep_async(1:i32).await
            waited6 += 1:i32
        }
        task_cancel<Result<i32, CloseError>>(task6)
        auto out6 = task6.await?
        match out6 {
            auto Option<Result<i32, CloseError>>::Some(res) => {
                match res {
                    auto Result<i32, CloseError>::Err(e) => {
                        if e.code != 303:i32 { return 61:i32 }
                    }
                    auto Result<i32, CloseError>::Ok(_) => { return 62:i32 }
                }
            }
            auto Option<Result<i32, CloseError>>::None => { return 63:i32 }
        }
        if g_live != 0:i32 { return 64:i32 }
    }

    // Path 7: Uncaught cancellation via race2 (concrete & generic) -> 2 guards
    {
        auto s7a = SessionA(res_guard = make_guard(70:i32), mode = 4:i32, base = 88:i32)
        auto w7a = race2<Result<i32, CloseError>, Result<i32, CloseError>>(
            cede fast_cancel_winner(),
            cede s7a.close()
        ).await
        match w7a {
            auto RaceWinner<Result<i32, CloseError>, Result<i32, CloseError>>::First(_) => {}
            auto RaceWinner<Result<i32, CloseError>, Result<i32, CloseError>>::Second(_) => { return 71:i32 }
        }
        if g_live != 0:i32 { return 72:i32 }

        auto s7b = SessionA(res_guard = make_guard(71:i32), mode = 4:i32, base = 99:i32)
        auto w7b = race2<Result<i32, CloseError>, Result<i32, CloseError>>(
            cede fast_cancel_winner(),
            cede close_any(cede s7b)
        ).await
        match w7b {
            auto RaceWinner<Result<i32, CloseError>, Result<i32, CloseError>>::First(_) => {}
            auto RaceWinner<Result<i32, CloseError>, Result<i32, CloseError>>::Second(_) => { return 73:i32 }
        }
        if g_live != 0:i32 { return 74:i32 }
    }

    if g_drops != 12:i32 {
        return 80:i32
    }
    return 0:i32
}

pub fn main() -> i32 {
    return block_on(run_all_7_paths())
}
"""
    },
    {
        "id": "pos2_borrowed_and_ref_carrier_local_await_and_generic",
        "description": "Local .await, block_on, and generic close_any<T: @AsyncClose> on BorrowedCarrier and RefCarrier(&view: i32, token: i32)",
        "source": COMMON_PREAMBLE + """
fn sync_consume_close<T: @AsyncClose>(cede x: T) -> i32 {
    auto r = block_on(close_any(cede x))
    match r {
        auto Result<i32, CloseError>::Ok(v) => { return v }
        auto Result<i32, CloseError>::Err(_) => { return -1:i32 }
    }
}

pub shape OwnedCarrier(pub value: i32)
impl OwnedCarrier@AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError> {
        auto owned = cede self
        sleep_async(1:i32).await
        return Result<i32, CloseError>::Ok(owned.value)
    }
}

fn test_async_borrow_consumers() -> async i32 {
    auto buf = Buffer(size = 40:i32)
    // 1. Direct c1.close().await on BorrowedCarrier
    auto c1 = BorrowedCarrier(inner = read_buf(buf), tag = 2:i32)
    auto r1 = c1.close().await
    match r1 {
        auto Result<i32, CloseError>::Ok(v) => { if v != 42:i32 { return 1:i32 } }
        auto Result<i32, CloseError>::Err(_) => { return 2:i32 }
    }

    // 2. Generic close_any(cede c2).await on BorrowedCarrier
    auto buf2 = Buffer(size = 40:i32)
    auto c2 = BorrowedCarrier(inner = read_buf(buf2), tag = 5:i32)
    auto r2 = close_any(cede c2).await
    match r2 {
        auto Result<i32, CloseError>::Ok(v) => { if v != 45:i32 { return 3:i32 } }
        auto Result<i32, CloseError>::Err(_) => { return 4:i32 }
    }

    auto local_x = 40:i32
    // 3. Direct ref1.close().await on RefCarrier(&view: i32, token: i32)
    auto ref1 = RefCarrier(&view = &local_x, token = 10:i32)
    auto r3 = ref1.close().await
    match r3 {
        auto Result<i32, CloseError>::Ok(v) => { if v != 50:i32 { return 5:i32 } }
        auto Result<i32, CloseError>::Err(_) => { return 6:i32 }
    }

    // 4. Generic close_any(cede ref2).await on RefCarrier(&view: i32, token: i32)
    auto ref2 = RefCarrier(&view = &local_x, token = 20:i32)
    auto r4 = close_any(cede ref2).await
    match r4 {
        auto Result<i32, CloseError>::Ok(v) => { if v != 60:i32 { return 7:i32 } }
        auto Result<i32, CloseError>::Err(_) => { return 8:i32 }
    }

    return 0:i32
}

pub fn main() -> i32 {
    auto rc = block_on(test_async_borrow_consumers())
    if rc != 0:i32 { return rc }

    auto buf3 = Buffer(size = 100:i32)
    // 5. sync_consume_close(cede c3) on BorrowedCarrier
    auto c3 = BorrowedCarrier(inner = read_buf(buf3), tag = 7:i32)
    if sync_consume_close(cede c3) != 107:i32 { return 9:i32 }

    auto local_y = 100:i32
    // 6. sync_consume_close(cede ref3) on RefCarrier
    auto ref3 = RefCarrier(&view = &local_y, token = 9:i32)
    if sync_consume_close(cede ref3) != 109:i32 { return 10:i32 }

    // 7. Owned carrier detached with .start and awaited via block_on
    auto owned1 = OwnedCarrier(value = 77:i32)
    auto t_owned = close_any(cede owned1)
    t_owned.start
    auto r_owned = block_on(t_owned)
    match r_owned {
        auto Result<i32, CloseError>::Ok(v) => { if v != 77:i32 { return 11:i32 } }
        auto Result<i32, CloseError>::Err(_) => { return 12:i32 }
    }

    return 0:i32
}
"""
    },
]

POS3_TKI_CONSUMER_SOURCE = """
import tki_close_provider::{CloseError, @AsyncClose, RefCarrier, close_any}
import std/task::{block_on, sleep_async}
import core/result::{Result}

pub shape LocalSession(pub code: i32)

impl LocalSession@AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError> {
        auto owned = cede self
        sleep_async(1:i32).await
        return Result<i32, CloseError>::Ok(owned.code + 10:i32)
    }
}

fn run_cross_module() -> async i32 {
    auto s = LocalSession(code = 32:i32)
    auto r1 = close_any(cede s).await
    match r1 {
        auto Result<i32, CloseError>::Ok(v) => { if v != 42:i32 { return 1:i32 } }
        auto Result<i32, CloseError>::Err(_) => { return 2:i32 }
    }

    auto local_v = 50:i32
    auto rc = RefCarrier(&view = &local_v, token = 8:i32)
    auto r2 = rc.close().await
    match r2 {
        auto Result<i32, CloseError>::Ok(v) => { if v != 58:i32 { return 3:i32 } }
        auto Result<i32, CloseError>::Err(_) => { return 4:i32 }
    }

    auto rc2 = RefCarrier(&view = &local_v, token = 12:i32)
    auto r3 = close_any(cede rc2).await
    match r3 {
        auto Result<i32, CloseError>::Ok(v) => { if v != 62:i32 { return 5:i32 } }
        auto Result<i32, CloseError>::Err(_) => { return 6:i32 }
    }
    return 0:i32
}

pub fn main() -> i32 {
    return block_on(run_cross_module())
}
"""


def run_cmd(cmd, cwd, env):
    res = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, timeout=120)
    return res.returncode, res.stdout, res.stderr


def build_tki_provider(tokac, toka_lib, env, tki_dir: pathlib.Path):
    tki_dir.mkdir(parents=True, exist_ok=True)
    src_path = tki_dir / "tki_close_provider.tk"
    tki_path = tki_dir / "tki_close_provider.tki"
    obj_path = tki_dir / "tki_close_provider.o"
    bodyless_tki_path = tki_dir / "bodyless_close_consumer.tki"
    src_path.write_text(TKI_PROVIDER_SOURCE.lstrip(), encoding="utf-8")
    bodyless_tki_path.write_bytes(BODYLESS_TKI_CONTENT.encode("utf-8"))

    rc1, out1, err1 = run_cmd(
        [str(tokac), "-I", str(toka_lib), "-c", "--emit-interface", str(src_path), "-o", str(obj_path)],
        cwd=tki_dir,
        env=env,
    )
    if rc1 != 0 or not tki_path.exists() or not obj_path.exists():
        raise RuntimeError(f"Failed to emit tki_close_provider.tki / .o:\n{out1}\n{err1}")

    # Remove source so consumers strictly use sourceless .tki
    src_path.unlink()
    return tki_path, obj_path


def verify_negative_case(tokac, toka_lib, probes_dir, work_dir, case, env, extra_include_dirs=None):
    cid = case["id"]
    src_file = probes_dir / f"{cid}.tk"
    src_file.write_text(case["source"].lstrip(), encoding="utf-8")

    case_work = work_dir / cid
    if case_work.exists():
        shutil.rmtree(case_work)
    case_work.mkdir(parents=True, exist_ok=True)

    inc_args = ["-I", str(toka_lib)]
    if extra_include_dirs:
        for d in extra_include_dirs:
            inc_args.extend(["-I", str(d)])

    modes = {
        "check-only": [str(tokac), *inc_args, "--check-only", str(src_file)],
        "compile-obj": [str(tokac), *inc_args, "-c", str(src_file), "-o", str(case_work / f"{cid}.o")],
        "emit-llvm": [str(tokac), *inc_args, "--emit-llvm", str(src_file), "-o", str(case_work / f"{cid}.ll")],
    }

    mode_results = {}
    all_ok = True
    for mode_name, cmd in modes.items():
        rc, out, err = run_cmd(cmd, cwd=case_work, env=env)
        combined = (out + "\n" + err).strip()
        leftover_files = [p.name for p in case_work.iterdir()]
        for ext in (".ll", ".o", ".obj", ".s", ".bc"):
            p = probes_dir / f"{cid}{ext}"
            if p.exists():
                leftover_files.append(str(p))

        expected = case["expected_code"]
        code_matched = expected in combined
        failed_closed = (rc != 0) and code_matched and (len(leftover_files) == 0)
        if not failed_closed:
            all_ok = False
        mode_results[mode_name] = {
            "rc": rc,
            "expected_code": expected,
            "code_matched": code_matched,
            "leftover_artifacts": leftover_files,
            "passed": failed_closed,
            "diagnostic_excerpt": combined.splitlines()[:6],
        }

    return {
        "id": cid,
        "category": case["category"],
        "description": case["description"],
        "passed": all_ok,
        "modes": mode_results,
    }


def verify_positive_case(tokac, toka_lib, probes_dir, work_dir, case, env, extra_include_dirs=None, extra_link_objs=None):
    cid = case["id"]
    src_file = probes_dir / f"{cid}.tk"
    src_file.write_text(case["source"].lstrip(), encoding="utf-8")

    case_work = work_dir / cid
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

    # 1. --check-only
    rc_chk, out_chk, err_chk = run_cmd(
        [str(tokac), *inc_args, "--check-only", str(src_file)],
        cwd=case_work,
        env=env,
    )
    # 2. --emit-llvm
    rc_ll, out_ll, err_ll = run_cmd(
        [str(tokac), *inc_args, "--emit-llvm", str(src_file), "-o", str(ll_file)],
        cwd=case_work,
        env=env,
    )
    ll_exists = ll_file.exists() and ll_file.stat().st_size > 0

    # 3. -c (object emission)
    rc_obj, out_obj, err_obj = run_cmd(
        [str(tokac), *inc_args, "-c", str(src_file), "-o", str(obj_file)],
        cwd=case_work,
        env=env,
    )

    # 4. Full binary build & native execution
    link_inputs = [str(src_file)]
    if extra_link_objs:
        link_inputs.extend(str(o) for o in extra_link_objs)
    rc_bin, out_bin, err_bin = run_cmd(
        [str(tokac), *inc_args, *link_inputs, "-o", str(bin_file)],
        cwd=case_work,
        env=env,
    )

    exec_rc = -1
    exec_out = ""
    exec_err = ""
    if rc_bin == 0 and bin_file.exists():
        exec_rc, exec_out, exec_err = run_cmd([str(bin_file)], cwd=case_work, env=env)

    passed = (
        rc_chk == 0
        and rc_ll == 0
        and ll_exists
        and rc_obj == 0
        and obj_file.exists()
        and rc_bin == 0
        and exec_rc == 0
    )

    return {
        "id": cid,
        "description": case["description"],
        "passed": passed,
        "modes": {
            "check-only": {"rc": rc_chk, "stderr": err_chk.strip()},
            "emit-llvm": {"rc": rc_ll, "ll_generated": ll_exists, "stderr": err_ll.strip()},
            "compile-obj": {"rc": rc_obj, "obj_generated": obj_file.exists(), "stderr": err_obj.strip()},
            "binary-link": {"rc": rc_bin, "stderr": err_bin.strip()},
            "execution": {"rc": exec_rc, "stdout": exec_out.strip(), "stderr": exec_err.strip()},
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, help="Path to compiler build directory")
    parser.add_argument("--output-receipt", default=None, help="Optional output path for JSON execution receipt")
    parser.add_argument("--work-dir", default=None, help="Optional scratch work directory")
    args = parser.parse_args()

    build_dir = pathlib.Path(args.build_dir).resolve()
    tokac = build_dir / "bin/tokac"
    if not tokac.exists() and (build_dir / "bin/tokac.exe").exists():
        tokac = build_dir / "bin/tokac.exe"
    elif not tokac.exists() and (build_dir / "bin/Release/tokac.exe").exists():
        tokac = build_dir / "bin/Release/tokac.exe"

    if not tokac.exists():
        print(f"[FAIL] missing compiler at: {tokac}", file=sys.stderr)
        sys.exit(1)

    toka_lib = pathlib.Path(os.environ.get("TOKA_LIB", str(ROOT / "lib"))).resolve()
    if not toka_lib.exists():
        print(f"[FAIL] missing toka standard library at: {toka_lib}", file=sys.stderr)
        sys.exit(1)

    temp_dir_obj = None
    if args.work_dir:
        base_work_dir = pathlib.Path(args.work_dir).resolve()
        base_work_dir.mkdir(parents=True, exist_ok=True)
    else:
        temp_dir_obj = tempfile.TemporaryDirectory(prefix="toka_018_v_")
        base_work_dir = pathlib.Path(temp_dir_obj.name).resolve()

    probes_dir = base_work_dir / "probes"
    probes_dir.mkdir(parents=True, exist_ok=True)
    work_dir = base_work_dir / "work"
    work_dir.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env["TOKA_LIB"] = str(toka_lib)

    tki_dir = work_dir / "tki_provider"
    if tki_dir.exists():
        shutil.rmtree(tki_dir)
    tki_path, tki_obj = build_tki_provider(tokac, toka_lib, env, tki_dir)
    print(f"[TKI] Built sourceless provider: {tki_path.name} and {tki_obj.name}")

    neg_results = []
    for case in NEGATIVE_CASES:
        res = verify_negative_case(tokac, toka_lib, probes_dir, work_dir, case, env)
        neg_results.append(res)
        status = "PASS" if res["passed"] else "FAIL"
        print(f"[{status}] Negative {res['id']} ({res['category']})")
        if not res["passed"]:
            print(json.dumps(res, indent=2))

    for case in TKI_NEGATIVE_CASES:
        res = verify_negative_case(tokac, toka_lib, probes_dir, work_dir, case, env, extra_include_dirs=[tki_dir])
        neg_results.append(res)
        status = "PASS" if res["passed"] else "FAIL"
        print(f"[{status}] Negative {res['id']} ({res['category']})")
        if not res["passed"]:
            print(json.dumps(res, indent=2))

    pos_results = []
    for case in POSITIVE_CASES:
        res = verify_positive_case(tokac, toka_lib, probes_dir, work_dir, case, env)
        pos_results.append(res)
        status = "PASS" if res["passed"] else "FAIL"
        print(f"[{status}] Positive {res['id']}")
        if not res["passed"]:
            print(json.dumps(res, indent=2))

    pos3_case = {
        "id": "pos3_cross_module_sourceless_tki_async_close",
        "description": "Cross-module sourceless .tki (checked-local-v1-0201) @AsyncClose trait impl, concrete & generic close_any consumption, and native execution",
        "source": POS3_TKI_CONSUMER_SOURCE,
    }
    res_pos3 = verify_positive_case(
        tokac,
        toka_lib,
        probes_dir,
        work_dir,
        pos3_case,
        env,
        extra_include_dirs=[tki_dir],
        extra_link_objs=[tki_obj],
    )
    pos_results.append(res_pos3)
    status = "PASS" if res_pos3["passed"] else "FAIL"
    print(f"[{status}] Positive {res_pos3['id']}")
    if not res_pos3["passed"]:
        print(json.dumps(res_pos3, indent=2))

    summary = {
        "schema": "toka.018-stage1-verification.v1",
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "negative_total": len(neg_results),
        "negative_passed": sum(1 for r in neg_results if r["passed"]),
        "positive_total": len(pos_results),
        "positive_passed": sum(1 for r in pos_results if r["passed"]),
        "negatives": neg_results,
        "positives": pos_results,
    }

    if args.output_receipt:
        receipt_path = pathlib.Path(args.output_receipt).resolve()
        receipt_path.parent.mkdir(parents=True, exist_ok=True)
        receipt_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
        print(f"[RECEIPT] Saved verification receipt to {receipt_path}")

    print(
        f"\nSummary: Negatives {summary['negative_passed']}/{summary['negative_total']}, "
        f"Positives {summary['positive_passed']}/{summary['positive_total']}"
    )

    if temp_dir_obj:
        temp_dir_obj.cleanup()

    if summary["negative_passed"] != summary["negative_total"] or summary["positive_passed"] != summary["positive_total"]:
        sys.exit(1)


if __name__ == "__main__":
    main()

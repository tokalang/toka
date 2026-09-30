# Migrating Vec code to the 0.11.0 candidate

This guide describes the pending `0.11.0` source candidate. It does not
announce public SDK assets. Published `v0.10.0` remains available unchanged.

## Storage and raw APIs

Vec's `buf`, `len` and `cap` no longer have public field grants. Replace direct
length reads with `len()` and element access with checked `borrow()` or the
appropriate safe operation. External code cannot construct `Vec<T>` directly
from its private fields, including inside an unsafe block.

`from_raw`, `unsafe_set_len`, `unsafe_forget`, `unsafe_as_raw`,
`unsafe_into_raw`, `unsafe_storage`, `unsafe_storage_mut` and `unsafe_borrow`
are unsafe methods. Call them only from an explicit unsafe context after
establishing their documented preconditions. A `from_raw` caller transfers a
compatible allocation of `cap` slots, proves `len <= cap` and the initialized
prefix, excludes conflicting live borrows, and takes sole cleanup
responsibility. Length mutation neither initializes new slots nor drops
retired slots. Adding `unsafe` around a call does not remove ordinary view
lifetime or PAL constraints.

## Owning values and borrowed values

For an independently owned value, continue to use explicit whole-value
transfer where required:

```toka
auto values# = Vec<string>::new()
auto text = string::from("owned")
values#.push(cede text)
```

Owning nested elements can be extracted and remain alive after their original
Vec is destroyed. The production lifecycle probe covers nonempty
`Vec<Item(children: Vec<Leaf>)>`, growth at both levels and exactly-once Drop.

A container's storage ownership does not make a stored borrowed view
independent. For `owner.as_str()` stored in a Vec, keep the actual owner alive
and avoid invalidating its buffer while any holder remains live. Retirement
of the last holder releases its loan. A copied extracted view can outlive
the Vec while its external owner lives; a new `borrow()` of an element still
depends on the Vec's storage. Destructors that read views also require their
sources to live through the existing reverse-declaration cleanup order.

If a wrapper mutates a receiver to retain an argument's external sources,
declare its effect explicitly:

```toka
pub fn push(self#, cede val: T)
effects:
    self <- val
```

The effect preserves existing receiver dependencies and joins the value's
actual external sources. It does not borrow the temporary argument slot or
grant new permissions. Extraction uses `return <- self.external` where it
forwards carried sources; do not substitute that for a newly created storage
borrow. The checked contracts are serialized symbolically in TKI and mapped
to actual binding identities at a call.

## Cache and precision boundaries

Regenerate TKI and semantic caches from the new compiler. Format `5` and
compiler-interface identity `0.9.9-38` reject older records. The public build
version changes to `0.11.0`; native layout/calling ABI is unchanged here.
Dependencies after `remove` or `clear` may be retained conservatively;
unknown sources remain unknown. Existing reference-element paths are
preserved, but this release does not promise unrestricted new `Vec<str>`
patterns or automatic proof of unsafe storage invariants.

## 中文迁移摘要

`0.11.0` 仍为待验证、待发布的集成候选。Vec 的 `buf/len/cap` 已封装，
外部代码使用 `len()`、`borrow()` 等接口；raw 构造、存储访问和长度修改
必须进入显式 unsafe，并承担分配、初始化范围、槽位退休与清理前置条件。
unsafe 不会抹去普通借用来源或授予可写权限。

拥有型嵌套元素可通过正式标准库移动、取出和独立存活。借用型元素仍依赖
真实 owner；新建元素借用仍依赖 Vec 存储。接收者保存值时，使用显式
`self <- val` 契约；提取已有外部依赖时使用合适的 `return <- self.external`
契约。删除元素后的依赖可保守保留，未知来源继续拒绝。旧 TKI 与语义缓存
需要重新生成，native ABI 不因本次版本迁移改变。

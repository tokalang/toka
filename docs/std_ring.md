# RingCore library representation

RingCore preserves its deque operations: new, len, is_empty, push_front/back,
pop_front/back, clear, and source-preserving get for T: @Dup. It is now backed
by two Vecs instead of a separately managed raw circular allocation. Raw
buf/cap/mask/head/len fields are no longer a public storage interface; consumers
use the deque methods. VecDeque and channel use only those methods.

Logical order is `reverse(front) ++ back`. Push/pop on a nonempty corresponding
side use Vec push/pop. If the requested side is empty, half the other side is
saved, the remaining half reverses into the requested side, and saved elements
return to their original side. This avoids full reversal on every alternating
end removal. An occasional refill is linear; end operations are amortized
constant time. get uses one index mapping and exactly one Dup operation.

## Responsibility and failure boundary

| Step | Owner of each complete element |
| --- | --- |
| Steady state | Exactly one live Vec slot in front or back |
| Successful pop in refill | Complete local value; source Vec has retired its tail |
| Successful push in refill | Destination Vec; local transfer responsibility is retired |
| Saved half | Local saved Vec until returned to source |
| clear / owner drop | Existing Vec cleanup for the two live prefixes |

Ring contains no raw_take, raw write, alloc/free, new unsafe precondition,
dynamic initialized-extent witness or compiler special case. The previously
rejected raw-retirement rewrite was not applied. This representation instead
reuses the existing full-value Vec contract. Source and destination are always
different Vec fields; the private refill helper is called only for an empty
target. During refill every intermediate owner is a valid Vec or a complete
local value, rather than a partly populated untracked raw allocation.

Allocation behavior follows Vec: no new recoverable allocation-error or unwind
protocol is promised. Refills may allocate a temporary half-buffer. clear/drop
delegate to Vec and do not refill/allocate; element destructor order follows
the two Vecs, not a FIFO destructor-order guarantee. The prior implementation
cleared elements front-to-back; that incidental destruction order is not
preserved. Callers requiring ordered destruction should explicitly pop and
dispose in their chosen order. Element order returned by pop/get is unchanged.
This representation changes storage/performance details,
not capture, shared reference counting, ABI/TKI, Copy/Dup or raw_take rules.
Unproved element dependencies remain subject to the existing Vec restrictions.

The gate `toka_ring_library` verifies a seeded model of 400 mixed operations,
growth/refill from both ends, empty results, resource/unique/shared exact-once,
an independent remaining shared owner, no-Drop NonCopy, string pressure,
explicit Dup, source invalidation and rejection without object/IR output.

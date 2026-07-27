# Verdict: detached non-borrowing work

## Observed result

Both programs launch work that receives a non-borrowing value and join it
later.  Rust's ordinary `thread::spawn` accepts the explicit `move` closure;
the paired Rust program without `move` is rejected because the detached thread
may outlive `value`.  Toka accepts the scalar `.start` input and awaits it
through `block_on`.

## Fair conclusion

The scoped-thread case is not evidence that Toka's detached boundary is
unusually strict.  Ordinary detached Rust threads also forbid a borrowed parent
capture; Rust's additional `thread::scope` protocol is what changes that
boundary.  Toka's explicit transfer rule for owned values is therefore a design
choice at the detached boundary, while lexical scoped borrowing remains a
separate extension opportunity.

#!/usr/bin/env python3
"""Strict five-program migration gate; unresolved positives must still fail it.

Retains 5x50000 shared-refcount iterations, 5x1000 mutex increments,
10x10000 atomic increments, the two producers and bounded capacity of two.
Uses the common runtime/parity runner; never converts a compiler rejection
into a passing negative test.
"""
import test_rc13_semver_thread_migration as runner

runner.CASES = (
    'g08_sync_mpsc_bounded.tk', 'g08_sync_mpsc_multi.tk',
    'g09_atomic_stress.tk', 'g09_mutex_stress.tk', 'g09_std_atomic.tk',
)

if __name__ == '__main__':
    runner.main()

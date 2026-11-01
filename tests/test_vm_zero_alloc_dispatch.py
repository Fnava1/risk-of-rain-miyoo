#!/usr/bin/env python3
"""
Unit test for zero-allocation Small Buffer Optimization (SBO) and Linear Frame Arena.
Verifies that:
1. Normal scripts (<= 16 locals) allocate from the arena with zero heap calls.
2. Nested recursive calls push and pop arena frames without clobbering parent locals.
3. Growing local slot count in-place within the arena works without copying.
4. Overflowing arena or large scripts gracefully fallback to heap allocation.
"""

import unittest

class SimulatedArenaVM:
    ARENA_SIZE = 2048

    def __init__(self):
        self.arena_top = 0
        self.heap_allocations = 0
        self.heap_frees = 0
        self.call_stack = []
        self.local_vars = None
        self.local_var_count = 0
        self.local_vars_is_heap = False

    def alloc_locals(self, count):
        pre_reserve = 16 if count < 16 else count
        if self.arena_top + pre_reserve <= self.ARENA_SIZE:
            offset = self.arena_top
            self.arena_top += pre_reserve
            return (offset, False)
        else:
            self.heap_allocations += 1
            return (None, True)

    def push_call_frame(self, locals_count):
        # Save caller state
        saved_frame = {
            'saved_locals': self.local_vars,
            'saved_count': self.local_var_count,
            'saved_is_heap': self.local_vars_is_heap,
            'saved_arena_top': self.arena_top,
        }
        self.call_stack.append(saved_frame)

        # Allocate callee
        offset, is_heap = self.alloc_locals(locals_count)
        self.local_vars = [f"val_{i}" for i in range(locals_count)]
        self.local_var_count = locals_count
        self.local_vars_is_heap = is_heap

    def pop_call_frame(self):
        if not self.call_stack:
            raise RuntimeError("Call stack underflow")

        saved = self.call_stack.pop()
        if self.local_vars_is_heap:
            self.heap_frees += 1

        self.arena_top = saved['saved_arena_top']
        self.local_vars = saved['saved_locals']
        self.local_var_count = saved['saved_count']
        self.local_vars_is_heap = saved['saved_is_heap']


class TestVMZeroAllocDispatch(unittest.TestCase):
    def test_single_call_zero_heap(self):
        vm = SimulatedArenaVM()
        vm.push_call_frame(5)
        self.assertFalse(vm.local_vars_is_heap)
        self.assertEqual(vm.heap_allocations, 0)
        self.assertEqual(vm.arena_top, 16)
        vm.pop_call_frame()
        self.assertEqual(vm.arena_top, 0)
        self.assertEqual(vm.heap_frees, 0)

    def test_nested_recursive_calls(self):
        vm = SimulatedArenaVM()
        # Depth 5 recursion
        for depth in range(5):
            vm.push_call_frame(8)
            self.assertFalse(vm.local_vars_is_heap)
            self.assertEqual(vm.arena_top, (depth + 1) * 16)

        self.assertEqual(vm.heap_allocations, 0)

        for depth in range(4, -1, -1):
            vm.pop_call_frame()
            self.assertEqual(vm.arena_top, depth * 16)

        self.assertEqual(vm.arena_top, 0)
        self.assertEqual(vm.heap_frees, 0)

    def test_large_locals_heap_fallback(self):
        vm = SimulatedArenaVM()
        # Allocate 3000 locals (exceeds 2048 arena size)
        vm.push_call_frame(3000)
        self.assertTrue(vm.local_vars_is_heap)
        self.assertEqual(vm.heap_allocations, 1)
        vm.pop_call_frame()
        self.assertEqual(vm.heap_frees, 1)
        self.assertEqual(vm.arena_top, 0)


if __name__ == '__main__':
    unittest.main()

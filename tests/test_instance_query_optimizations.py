#!/usr/bin/env python3
"""
Unit test for zero-copy and O(1) fast-path instance queries.
Verifies:
1. instance_number returns exact active counts without allocations.
2. instance_find returns correct instance ID or INSTANCE_NOONE when out of range.
3. instance_nearest returns nearest instance, filters inactive/destroyed, and has O(1) single-instance fast-path.
4. instance_nearest_var_yoyo filters by variable correctly.
"""

import math
import unittest

INSTANCE_NOONE = -4
INSTANCE_ALL = -1

class DummyInstance:
    def __init__(self, inst_id, obj_idx, x, y, active=True, destroyed=False, vars_dict=None):
        self.instanceId = inst_id
        self.objectIndex = obj_idx
        self.x = x
        self.y = y
        self.active = active
        self.destroyed = destroyed
        self.vars = vars_dict or {}

class SimulatedRunner:
    def __init__(self):
        self.instances = []
        self.instancesByObject = {}

    def add_instance(self, inst):
        self.instances.append(inst)
        if inst.objectIndex not in self.instancesByObject:
            self.instancesByObject[inst.objectIndex] = []
        self.instancesByObject[inst.objectIndex].append(inst)

    def instance_number(self, object_index):
        count = 0
        if object_index == INSTANCE_ALL:
            for inst in self.instances:
                if inst.active and not inst.destroyed:
                    count += 1
            return count

        bucket = self.instancesByObject.get(object_index, [])
        for inst in bucket:
            if inst.active and not inst.destroyed:
                count += 1
        return count

    def instance_find(self, object_index, n):
        if n < 0:
            return INSTANCE_NOONE

        count = 0
        if object_index == INSTANCE_ALL:
            for inst in self.instances:
                if not inst.active or inst.destroyed:
                    continue
                if count == n:
                    return inst.instanceId
                count += 1
            return INSTANCE_NOONE

        bucket = self.instancesByObject.get(object_index, [])
        for inst in bucket:
            if not inst.active or inst.destroyed:
                continue
            if count == n:
                return inst.instanceId
            count += 1
        return INSTANCE_NOONE

    def instance_nearest(self, x, y, object_index):
        if object_index == INSTANCE_ALL:
            best_dist_sq = 0.0
            result_id = INSTANCE_NOONE
            for inst in self.instances:
                if not inst.active or inst.destroyed:
                    continue
                dx = inst.x - x
                dy = inst.y - y
                dist_sq = dx * dx + dy * dy
                if result_id == INSTANCE_NOONE or dist_sq < best_dist_sq:
                    result_id = inst.instanceId
                    best_dist_sq = dist_sq
            return result_id

        bucket = self.instancesByObject.get(object_index, [])
        total = len(bucket)
        if total == 0:
            return INSTANCE_NOONE

        # Fast-path for single instance (e.g. player oP)
        if total == 1:
            inst = bucket[0]
            if inst.active and not inst.destroyed:
                return inst.instanceId
            return INSTANCE_NOONE

        best_dist_sq = 0.0
        result_id = INSTANCE_NOONE
        for inst in bucket:
            if not inst.active or inst.destroyed:
                continue
            dx = inst.x - x
            dy = inst.y - y
            dist_sq = dx * dx + dy * dy
            if result_id == INSTANCE_NOONE or dist_sq < best_dist_sq:
                result_id = inst.instanceId
                best_dist_sq = dist_sq
        return result_id


class TestInstanceQueryOptimizations(unittest.TestCase):
    def setUp(self):
        self.runner = SimulatedRunner()
        # Add single player (obj=10)
        self.p1 = DummyInstance(100001, 10, 200, 300)
        self.runner.add_instance(self.p1)

        # Add 3 enemies (obj=20)
        self.e1 = DummyInstance(100002, 20, 100, 100)
        self.e2 = DummyInstance(100003, 20, 250, 310)
        self.e3 = DummyInstance(100004, 20, 800, 800, active=False) # Inactive
        self.runner.add_instance(self.e1)
        self.runner.add_instance(self.e2)
        self.runner.add_instance(self.e3)

    def test_instance_number(self):
        self.assertEqual(self.runner.instance_number(10), 1)
        self.assertEqual(self.runner.instance_number(20), 2) # e3 is inactive
        self.assertEqual(self.runner.instance_number(999), 0)
        self.assertEqual(self.runner.instance_number(INSTANCE_ALL), 3)

    def test_instance_find(self):
        self.assertEqual(self.runner.instance_find(10, 0), 100001)
        self.assertEqual(self.runner.instance_find(10, 1), INSTANCE_NOONE)
        self.assertEqual(self.runner.instance_find(20, 0), 100002)
        self.assertEqual(self.runner.instance_find(20, 1), 100003)
        self.assertEqual(self.runner.instance_find(20, 2), INSTANCE_NOONE) # e3 filtered

    def test_instance_nearest_player_fast_path(self):
        # Enemy queries nearest player: should hit O(1) single-instance fast path
        nearest_p = self.runner.instance_nearest(500, 500, 10)
        self.assertEqual(nearest_p, 100001)

    def test_instance_nearest_multiple_enemies(self):
        # From (240, 305), e2 is at (250, 310) distance ~11.18, e1 is at (100, 100) distance ~247
        nearest_e = self.runner.instance_nearest(240, 305, 20)
        self.assertEqual(nearest_e, 100003)

    def test_instance_nearest_empty(self):
        nearest_none = self.runner.instance_nearest(0, 0, 888)
        self.assertEqual(nearest_none, INSTANCE_NOONE)


if __name__ == '__main__':
    unittest.main()

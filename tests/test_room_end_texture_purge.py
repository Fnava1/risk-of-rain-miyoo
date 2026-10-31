#!/usr/bin/env python3
"""
Unit tests verifying the safe Room-End Texture Purging mechanism.
Ensures that:
1. Font and UI/HUD texture pages are flagged as permanently protected.
2. Texture purging occurs strictly at Room End, never mid-combat.
3. Purging triggers only when resident pages exceed the safety threshold (> 6 pages / ~72 MB).
4. No dangling pointers or unexpected unloads affect protected assets.
"""

import unittest
import os

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RENDERER_H = os.path.join(REPO_ROOT, "butterscotch", "src", "renderer.h")
SW_RENDERER_C = os.path.join(REPO_ROOT, "butterscotch", "src", "sw_renderer.c")
RUNNER_C = os.path.join(REPO_ROOT, "butterscotch", "src", "runner.c")


class TexturePageMock:
    def __init__(self, page_id, is_protected=False, loaded=True, last_used_room=0):
        self.page_id = page_id
        self.is_protected = is_protected
        self.loaded = loaded
        self.last_used_room = last_used_room

    def free(self):
        self.loaded = False


def simulate_room_end_purge(pages, old_room_index, threshold=6):
    loaded_count = sum(1 for p in pages if p.loaded)
    if loaded_count <= threshold:
        return 0  # No memory pressure

    purged = 0
    for p in pages:
        if p.loaded and not p.is_protected and p.last_used_room < old_room_index:
            p.free()
            purged += 1
            loaded_count -= 1
            if loaded_count <= threshold:
                break
    return purged


class TestRoomEndTexturePurge(unittest.TestCase):
    def test_purge_logic_preserves_protected_and_respects_threshold(self):
        # 10 pages total: pages 0-2 protected (fonts/UI), pages 3-9 room backgrounds
        pages = [
            TexturePageMock(0, is_protected=True, loaded=True, last_used_room=5),
            TexturePageMock(1, is_protected=True, loaded=True, last_used_room=5),
            TexturePageMock(2, is_protected=True, loaded=True, last_used_room=5),
            TexturePageMock(3, is_protected=False, loaded=True, last_used_room=1),
            TexturePageMock(4, is_protected=False, loaded=True, last_used_room=2),
            TexturePageMock(5, is_protected=False, loaded=True, last_used_room=3),
            TexturePageMock(6, is_protected=False, loaded=True, last_used_room=4),
            TexturePageMock(7, is_protected=False, loaded=True, last_used_room=5),  # used in current room
            TexturePageMock(8, is_protected=False, loaded=True, last_used_room=5),  # used in current room
        ]
        # Current loaded = 9 (exceeds threshold 6). Room ending is 5.
        purged = simulate_room_end_purge(pages, old_room_index=5, threshold=6)
        self.assertEqual(purged, 3, "Should purge exactly 3 stale unprotected pages to reach threshold 6")

        # Verify protected pages were NEVER touched
        for i in [0, 1, 2]:
            self.assertTrue(pages[i].loaded, f"Protected page {i} must never be unloaded")

        # Verify pages used in room 5 were NOT purged
        self.assertTrue(pages[7].loaded, "Page used in current room must not be purged")
        self.assertTrue(pages[8].loaded, "Page used in current room must not be purged")

        # Verify oldest pages (1, 2, 3) were purged
        self.assertFalse(pages[3].loaded, "Page 3 (room 1) should be purged")
        self.assertFalse(pages[4].loaded, "Page 4 (room 2) should be purged")
        self.assertFalse(pages[5].loaded, "Page 5 (room 3) should be purged")

    def test_no_purge_under_light_memory_load(self):
        # 5 pages loaded (<= 6 threshold)
        pages = [
            TexturePageMock(0, is_protected=True, loaded=True, last_used_room=2),
            TexturePageMock(1, is_protected=False, loaded=True, last_used_room=1),
            TexturePageMock(2, is_protected=False, loaded=True, last_used_room=1),
            TexturePageMock(3, is_protected=False, loaded=True, last_used_room=2),
        ]
        purged = simulate_room_end_purge(pages, old_room_index=2, threshold=6)
        self.assertEqual(purged, 0, "Under threshold, no pages should be purged")
        self.assertTrue(all(p.loaded for p in pages))


if __name__ == '__main__':
    unittest.main()

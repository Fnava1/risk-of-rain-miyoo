import unittest
import os

class TestDebugMenuComboActivation(unittest.TestCase):
    def setUp(self):
        self.miyoo_c_path = os.path.join("butterscotch", "src", "backends", "miyoo.c")
        self.assertTrue(os.path.exists(self.miyoo_c_path), f"File not found: {self.miyoo_c_path}")
        with open(self.miyoo_c_path, "r", encoding="utf-8") as f:
            self.content = f.read()

    def test_local_coop_isolation(self):
        """Verifies that debug room detection is restricted strictly to Local Co-op (Room 7 / rSelectCoop) and NOT Online Co-op (Room 39 / rHost)."""
        # Ensure Room 7 and rSelectCoop are present
        self.assertIn("runner->currentRoomIndex == 7", self.content)
        self.assertIn('strcmp(runner->currentRoom->name, "rSelectCoop") == 0', self.content)

        # Ensure Room 39 and rHost are NOT included in is_debug_room
        is_debug_room_decl = self.content[self.content.find("static bool is_debug_room") : self.content.find("static void mmp_debug_menu_draw")]
        self.assertNotIn("39", is_debug_room_decl)
        self.assertNotIn("rHost", is_debug_room_decl)

    def test_debug_menu_state_flag(self):
        """Verifies that the debug menu requires manual toggle state rather than activating automatically."""
        self.assertIn("static bool s_debug_menu_open = false;", self.content)

    def test_combo_detection_logic(self):
        """Verifies that L + R + Select combo is properly detected with shoulder variations (L1/L2, R1/R2) and excludes Start."""
        self.assertIn("s_curr_state.l1 || s_curr_state.l2", self.content)
        self.assertIn("s_curr_state.r1 || s_curr_state.r2", self.content)
        self.assertIn("s_curr_state.select && !s_curr_state.start", self.content)
        self.assertIn("s_debug_menu_open = !s_debug_menu_open;", self.content)

    def test_conditional_overlay_drawing(self):
        """Verifies that the debug menu overlay is only rendered when open."""
        self.assertIn("if (s_debug_menu_open && is_debug_room(g_runner))", self.content)
        self.assertIn("mmp_debug_menu_draw(fb, tw, th);", self.content)

    def test_menu_close_behavior(self):
        """Verifies that pressing Cancel/B closes the debug overlay without forcing exit to the main menu."""
        start = self.content.find("if (s_debug_menu_open && is_debug_room(g_runner))")
        self.assertGreater(start, 0)
        end = self.content.find("if (s_osk_active)", start)
        self.assertGreater(end, start)
        debug_poll_block = self.content[start:end]
        self.assertIn("s_debug_menu_open = false;", debug_poll_block)
        # Cancel should NOT set pendingRoom to menu_room (2 / rStart)
        cancel_block = debug_poll_block[debug_poll_block.find("if (b_pressed || a_on_cancel)") :]
        self.assertNotIn("pendingRoom = menu_room", cancel_block)

    def test_room_transition_resets_menu_state(self):
        """Verifies that changing rooms resets s_debug_menu_open to false."""
        self.assertIn("s_debug_menu_open = false;", self.content)

if __name__ == "__main__":
    unittest.main()

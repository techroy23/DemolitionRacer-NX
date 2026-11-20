"""Check that Switch holds the original return-to-menu loading callback."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LoadingTransitionTest(unittest.TestCase):
    def test_return_to_menu_hold_follows_original_callback(self):
        source = (ROOT / 'src/game_main.c').read_text(encoding='utf-8')
        self.assertRegex(
            source,
            r'sub_8002B88C\(r_u32\(0x80065238u\), 0x800102D4u\);\s+'
            r'sub_8002B6CC\(\);\s+#if defined\(AP_SWITCH\)\s+'
            r'(?:\s*//[^\n]*\n)*\s*switch_hold_loading_screen\("Return to menu loading"\);'
            r'\s+#endif\s+\}',
        )

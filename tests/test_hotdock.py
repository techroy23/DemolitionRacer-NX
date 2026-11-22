import re
import unittest
from pathlib import Path

PLATFORM = Path(__file__).resolve().parents[1] / 'src/platform/switch'


class HotDockTests(unittest.TestCase):
    def test_mode_change_is_not_a_presenter_failure(self):
        presenter = (PLATFORM / 'docked_presenter.c').read_text()
        mismatch = re.search(r'if \(appletGetOperationMode\(\) != presenter\.operation_mode\)\s*\{([^}]+)\}', presenter)
        self.assertIsNotNone(mismatch)
        self.assertIn('return DOCKED_PRESENTER_MODE_CHANGED;', mismatch[1])

    def test_mode_change_releases_old_producer_and_can_retry_gles(self):
        platform = (PLATFORM / 'platform.c').read_text()
        match = re.search(r'static bool rebuild_gpu_display_for_mode_change\(void\)\s*\{([^}]+)\}', platform)
        self.assertIsNotNone(match)
        body = match[1]
        self.assertLess(body.index('docked_presenter_close();'), body.index('xport_window_init()'))
        self.assertLess(body.index('gpu_attempted = false;'), body.index('xport_window_init()'))
        self.assertLess(body.index('video_initialized = false;'), body.index('xport_window_init()'))
        frame = platform.split('sint32 xport_present(', 1)[1].split('RESUME_BEGIN(dequeue)', 1)[0]
        self.assertIn('result == DOCKED_PRESENTER_MODE_CHANGED', frame)
        self.assertIn('rebuild_gpu_display_for_mode_change()', frame)
        self.assertIn('result == DOCKED_PRESENTER_UNAVAILABLE', frame)
        self.assertIn('fall_back_to_software_display()', frame)

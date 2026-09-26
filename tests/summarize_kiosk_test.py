"""Missing firmware observations must remain unknown in report summaries."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "summarize_kiosk", Path(__file__).resolve().parents[1] / "scripts/summarize-kiosk.py")
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


class SummaryTest(unittest.TestCase):
    def summarize(self, values):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "samples.jsonl"
            path.write_text("\n".join(json.dumps({
                "condition": "test", "elapsed_s": 5, "temp_c": None,
                "system_cpu_pct_all_cores": 0, "throttled": value,
                "processes": [], "frequency_khz": {}, "memory": {}, "swap_io": {},
            }) for value in values))
            return summary.summarize(path)

    def test_missing_final_value(self):
        result = self.summarize([0x10001, None])
        self.assertIsNone(result["history_throttle_hex"])
        self.assertEqual(result["current_throttle_samples"], 1)

    def test_all_missing(self):
        self.assertIsNone(self.summarize([None, None])["history_throttle_hex"])

    def test_valid_final_value(self):
        self.assertEqual(self.summarize([None, 0x50000])["history_throttle_hex"], "0x50000")


if __name__ == "__main__":
    unittest.main()

"""Hardware-independent CDP transport regression tests."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location(
    "measure_kiosk_cdp", Path(__file__).resolve().parents[1] / "scripts/measure-kiosk-cdp.py")
cdp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cdp)


class CdpTest(unittest.TestCase):
    def test_handshake_eof_closes_socket(self):
        sock = Mock()
        sock.recv.side_effect = [b"HTTP/1.1 101", b"", AssertionError("read after EOF")]
        with patch.object(cdp.socket, "create_connection", return_value=sock):
            with self.assertRaisesRegex(RuntimeError, "closed during handshake"):
                cdp.WebSocket("ws://127.0.0.1:9222/devtools/page/test")
        sock.close.assert_called_once()

    def test_handshake_header_is_bounded(self):
        sock = Mock()
        sock.recv.return_value = b"x" * 4096
        with patch.object(cdp.socket, "create_connection", return_value=sock):
            with self.assertRaisesRegex(RuntimeError, "header too large"):
                cdp.WebSocket("ws://127.0.0.1:9222/devtools/page/test")
        sock.close.assert_called_once()

    def test_trace_events_before_and_after_response(self):
        data = {"method": "Tracing.dataCollected", "params": {"value": [
            {"name": "Paint"}, {"name": "Layout"}, {"name": "other"}]}}
        complete = {"method": "Tracing.tracingComplete"}
        response = {"id": 1, "result": {}}
        for messages in ([data, complete, response], [response, data, complete],
                         [data, response, data, complete]):
            with self.subTest(messages=messages):
                ws = cdp.WebSocket.__new__(cdp.WebSocket)
                ws.socket = Mock()
                ws.next_id = 0
                ws.events = {}
                ws.receive = Mock(side_effect=messages)
                count = messages.count(data)
                self.assertEqual(cdp.finish_trace(ws), {"Paint": count, "Layout": count})
                self.assertEqual(ws.receive.call_count, len(messages))

    def test_incomplete_trace_raises(self):
        ws = cdp.WebSocket.__new__(cdp.WebSocket)
        ws.socket = Mock()
        ws.next_id = 0
        ws.events = {}
        ws.receive = Mock(side_effect=[{"id": 1, "result": {}}, TimeoutError("missing completion")])
        with self.assertRaises(TimeoutError):
            cdp.finish_trace(ws)


if __name__ == "__main__":
    unittest.main()

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
                ws.deadline = None
                ws.socket = Mock()
                ws.next_id = 0
                ws.events = {}
                ws.receive = Mock(side_effect=messages)
                count = messages.count(data)
                self.assertEqual(cdp.finish_trace(ws), {"Paint": count, "Layout": count})
                self.assertEqual(ws.receive.call_count, len(messages))

    def test_incomplete_trace_raises(self):
        ws = cdp.WebSocket.__new__(cdp.WebSocket)
        ws.deadline = None
        ws.socket = Mock()
        ws.next_id = 0
        ws.events = {}
        ws.receive = Mock(side_effect=[{"id": 1, "result": {}}, TimeoutError("missing completion")])
        with self.assertRaises(TimeoutError):
            cdp.finish_trace(ws)

    def test_websocket_payload_bytes_count_only_received_text_payloads(self):
        ws = cdp.WebSocket.__new__(cdp.WebSocket)
        ws.deadline = None
        ws.socket = Mock()
        ws.next_id = 0
        ws.events = {}
        ws.event_payload_bytes = {}
        ws.receive = Mock(side_effect=[
            {"method": "Network.webSocketFrameReceived",
             "params": {"response": {"payloadData": "測定"}}},
            {"method": "Network.webSocketFrameReceived", "params": {"response": {}}},
            {"id": 1, "result": {}},
        ])

        ws.call("Performance.enable")

        self.assertEqual(ws.events["Network.webSocketFrameReceived"], 2)
        self.assertEqual(ws.event_payload_bytes["Network.webSocketFrameReceived"],
                         len("測定".encode("utf-8")))

    def test_expired_deadline_does_not_connect(self):
        with patch.object(cdp.time, 'monotonic', return_value=10), \
                patch.object(cdp.socket, 'create_connection') as connect:
            with self.assertRaisesRegex(TimeoutError, 'deadline expired'):
                cdp.WebSocket('ws://127.0.0.1:9223/socket/1/2/WebPage', deadline=9)
        connect.assert_not_called()

    def test_deadline_bounds_trickling_handshake(self):
        sock = Mock()
        sock.recv.return_value = b'H'
        with patch.object(cdp.socket, 'create_connection', return_value=sock), \
                patch.object(cdp.time, 'monotonic', side_effect=[0, 0, 1, 2, 3]):
            with self.assertRaisesRegex(TimeoutError, 'deadline expired'):
                cdp.WebSocket('ws://127.0.0.1:9223/socket/1/2/WebPage', deadline=3)
        self.assertEqual(sock.recv.call_count, 2)
        sock.close.assert_called_once()

    def test_deadline_bounds_trickling_frame(self):
        ws = cdp.WebSocket.__new__(cdp.WebSocket)
        ws.deadline = 3
        ws.socket = Mock()
        ws.socket.recv.return_value = b'x'
        ws.pending = b''
        with patch.object(cdp.time, 'monotonic', side_effect=[0, 1, 2, 3]):
            with self.assertRaisesRegex(TimeoutError, 'deadline expired'):
                ws.read_exact(100)
        self.assertEqual(ws.socket.recv.call_count, 2)

    def test_deadline_bounds_events_without_ack(self):
        ws = cdp.WebSocket.__new__(cdp.WebSocket)
        ws.deadline = 3
        ws.socket = Mock()
        ws.next_id = 0
        ws.events = {}
        ws.event_payload_bytes = {}
        event = cdp.json.dumps({'method': 'Runtime.event'}).encode()
        ws.pending = (bytes([0x81, len(event)]) + event) * 10
        with patch.object(cdp.time, 'monotonic', side_effect=[0, 1, 1, 2, 2, 3]):
            with self.assertRaisesRegex(TimeoutError, 'deadline expired'):
                ws.call('Target.sendMessageToTarget')
        self.assertEqual(ws.events, {'Runtime.event': 2})
        ws.socket.recv.assert_not_called()

    def test_reset_event_counters_preserves_transport_state(self):
        ws = cdp.WebSocket.__new__(cdp.WebSocket)
        ws.events = {"Network.webSocketFrameReceived": 3}
        ws.event_payload_bytes = {"Network.webSocketFrameReceived": 42}
        ws.next_id = 7

        ws.reset_event_counters()

        self.assertEqual(ws.events, {})
        self.assertEqual(ws.event_payload_bytes, {})
        self.assertEqual(ws.next_id, 7)


if __name__ == "__main__":
    unittest.main()

import importlib.util
import base64
from contextlib import contextmanager
import json
import os
from pathlib import Path
import struct
import tempfile
import time
from types import SimpleNamespace
import unittest
import zlib
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('wpe_inspector', Path(__file__).resolve().parents[1] / 'scripts/inspect-wpe.py')
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


class Socket:
    def settimeout(self, value):
        assert 0 < value <= 5


class WebSocket:
    def __init__(self):
        self.socket = Socket()
        self.sent = []

    def receive(self):
        return {'method': 'Target.targetCreated', 'params': {'targetInfo': {'targetId': 'page-1', 'type': 'page'}}}

    def call(self, method, params, on_event):
        self.sent.append((method, params))
        inner = json.loads(params['message'])
        on_event({'method': 'Target.dispatchMessageFromTarget', 'params': {'targetId': 'page-1',
            'message': json.dumps({'id': inner['id'], 'result': {'value': 'ok'}})}})
        return {}


class InspectorTest(unittest.TestCase):
    @staticmethod
    def png(extra=b''):
        def chunk(kind, data):
            return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
        return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!IIBBBBB', 1, 1, 8, 2, 0, 0, 0)) +
            (chunk(b'tEXt', b'Description\0' + extra) if extra else b'') +
            chunk(b'IDAT', zlib.compress(b'\0\x10\x20\x30')) + chunk(b'IEND', b''))

    @staticmethod
    def observation(width=1, height=1):
        return {'result': {'value': json.dumps({'width': width, 'height': height})}}

    def test_snapshot_routes_and_saves_private_png(self):
        data = self.png()
        client = Mock()
        client.call.return_value = {'dataURL': 'data:image/png;base64,' + base64.b64encode(data).decode()}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'snapshot.png'
            result = inspector.save_screenshot(client, self.observation(), path)
            self.assertEqual(path.read_bytes(), data)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            self.assertEqual(result, {'path': str(path), 'width': 1, 'height': 1, 'bytes': len(data)})
        client.call.assert_called_once_with('Page.snapshotRect', {
            'x': 0, 'y': 0, 'width': 1, 'height': 1, 'coordinateSystem': 'Viewport'})

    def test_snapshot_never_overwrites_existing_file_or_symlink(self):
        client = Mock()
        client.call.return_value = {'dataURL': 'data:image/png;base64,' + base64.b64encode(self.png()).decode()}
        with tempfile.TemporaryDirectory() as directory:
            original = Path(directory) / 'original'
            original.write_bytes(b'unchanged')
            link = Path(directory) / 'link.png'
            link.symlink_to(original)
            for path in (original, link):
                with self.assertRaises(FileExistsError):
                    inspector.save_screenshot(client, self.observation(), path)
            self.assertEqual(original.read_bytes(), b'unchanged')

    def test_snapshot_rejects_bad_response_without_creating_file(self):
        valid = self.png()
        damaged = bytearray(valid)
        damaged[29] ^= 1
        cases = ({}, {'dataURL': 'data:image/jpeg;base64,AAAA'},
            {'dataURL': 'data:image/png;base64,???'},
            {'dataURL': 'data:image/png;base64,' + base64.b64encode(b'not a PNG').decode()},
            {'dataURL': 'data:image/png;base64,' + base64.b64encode(damaged).decode()},
            {'dataURL': 'data:image/png;base64,' + base64.b64encode(valid[:-12]).decode()})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'snapshot.png'
            for response in cases:
                with self.subTest(response_keys=list(response)):
                    client = Mock()
                    client.call.return_value = response
                    with self.assertRaises(RuntimeError):
                        inspector.save_screenshot(client, self.observation(), path)
                    self.assertFalse(path.exists())

    def test_snapshot_rejects_oversized_png(self):
        client = Mock()
        client.call.return_value = {'dataURL': 'data:image/png;base64,' +
            base64.b64encode(self.png(b'x' * (8 * 1024 * 1024))).decode()}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'snapshot.png'
            with self.assertRaisesRegex(RuntimeError, 'byte limit'):
                inspector.save_screenshot(client, self.observation(), path)
            self.assertFalse(path.exists())

    def test_snapshot_rejects_invalid_viewport_before_request(self):
        client = Mock()
        for observation in ({}, self.observation(True), self.observation(0),
                            self.observation(3841), self.observation(3840, 3840)):
            with self.assertRaises(RuntimeError):
                inspector.save_screenshot(client, observation, Path('unused.png'))
        client.call.assert_not_called()

    def test_snapshot_removes_partial_output_on_write_failure(self):
        client = Mock()
        client.call.return_value = {'dataURL': 'data:image/png;base64,' + base64.b64encode(self.png()).decode()}
        real_fdopen = os.fdopen
        @contextmanager
        def fail_open(descriptor, mode):
            with real_fdopen(descriptor, mode) as output:
                def fail_write(contents):
                    output.write(contents[:8])
                    output.flush()
                    raise OSError('fixture write failure')
                yield SimpleNamespace(write=fail_write)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'snapshot.png'
            with patch.object(inspector.os, 'fdopen', side_effect=fail_open):
                with self.assertRaises(OSError):
                    inspector.save_screenshot(client, self.observation(), path)
            self.assertFalse(path.exists())

    def test_cli_snapshot_and_failure_close_connection(self):
        opener = Mock()
        opener.open.return_value.__enter__ = Mock(return_value=SimpleNamespace(read=lambda size:
            b'<tr><div class="targeturl">http://127.0.0.1:8080/player</div>'
            b'<input onclick="/socket/1/2/WebPage"></tr>'))
        opener.open.return_value.__exit__ = Mock(return_value=False)
        data = self.png()
        for snapshot in ({'dataURL': 'data:image/png;base64,' + base64.b64encode(data).decode()}, {}):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'snapshot.png'
                websocket = Mock()
                transport = SimpleNamespace(WebSocket=Mock(return_value=websocket))
                client = Mock()
                client.call.side_effect = [{}, self.observation(), snapshot]
                with patch('sys.argv', ['inspect-wpe.py', '--screenshot', str(path)]), \
                        patch.object(inspector, 'build_opener', return_value=opener), \
                        patch.object(inspector.importlib.util, 'module_from_spec', return_value=transport), \
                        patch.object(inspector.importlib.util, 'spec_from_file_location', return_value=Mock()), \
                        patch.object(inspector, 'PageClient', return_value=client), \
                        patch('builtins.print') as output:
                    if snapshot:
                        inspector.main()
                        self.assertEqual(path.read_bytes(), data)
                        self.assertEqual(json.loads(output.call_args.args[0])['screenshot']['bytes'], len(data))
                    else:
                        with self.assertRaises(RuntimeError): inspector.main()
                        output.assert_not_called()
                websocket.socket.close.assert_called_once()
                self.assertEqual(transport.WebSocket.call_args.kwargs['maximum_message_bytes'], 16 * 1024 * 1024)

    def test_cli_rejects_existing_path_and_conflicting_modes_before_network(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'existing.png'
            path.write_bytes(b'unchanged')
            for arguments in (['--screenshot', str(path)], ['--list', '--screenshot', str(path)],
                              ['--screenshot', str(Path(directory) / 'missing' / 'image.png')]):
                with patch('sys.argv', ['inspect-wpe.py', *arguments]), \
                        patch.object(inspector, 'build_opener') as network, patch('sys.stderr'):
                    with self.assertRaises(SystemExit) as error: inspector.main()
                    self.assertEqual(error.exception.code, 2)
                    network.assert_not_called()
            self.assertEqual(path.read_bytes(), b'unchanged')

    def test_listing(self):
        parser = inspector.Targets()
        parser.feed('''<tr><div class="targeturl">http://127.0.0.1:8080/player</div>
          <input onclick="window.open('Main.html?ws='+window.location.host+'/socket/17/23/WebPage')"></tr>''')
        self.assertEqual(parser.targets, [{'url': 'http://127.0.0.1:8080/player', 'path': '/socket/17/23/WebPage'}])

    def test_routed_reply_before_outer_ack(self):
        websocket = WebSocket()
        client = inspector.PageClient(websocket, time.monotonic() + 2)
        client.discover()
        self.assertEqual(client.call('Runtime.evaluate', {'expression': 'document.title'}), {'value': 'ok'})
        self.assertEqual(websocket.sent[0][0], 'Target.sendMessageToTarget')
        self.assertEqual(json.loads(websocket.sent[0][1]['message'])['method'], 'Runtime.evaluate')

    def test_paused_and_destroyed(self):
        client = inspector.PageClient(WebSocket(), time.monotonic() + 2)
        with self.assertRaisesRegex(RuntimeError, 'paused'):
            client.event({'method': 'Target.targetCreated', 'params': {'targetInfo':
                {'targetId': 'page-1', 'type': 'page', 'isPaused': True}}})
        client.discover()
        with self.assertRaisesRegex(RuntimeError, 'destroyed'):
            client.event({'method': 'Target.targetDestroyed', 'params': {'targetId': 'page-1'}})

    def test_deadline(self):
        client = inspector.PageClient(WebSocket(), time.monotonic() - 1)
        with self.assertRaises(TimeoutError):
            client.discover()

    def test_provisional_and_other_target_replies_are_ignored(self):
        client = inspector.PageClient(WebSocket(), time.monotonic() + 2)
        client.event({'method': 'Target.targetCreated', 'params': {'targetInfo':
            {'targetId': 'provisional', 'type': 'page', 'isProvisional': True}}})
        self.assertIsNone(client.target)
        client.discover()
        client.event({'method': 'Target.dispatchMessageFromTarget', 'params':
            {'targetId': 'other-page', 'message': json.dumps({'id': 1, 'result': {}})}})
        self.assertEqual(client.responses, {})

    def test_routed_reply_after_outer_ack(self):
        websocket = WebSocket()
        client = inspector.PageClient(websocket, time.monotonic() + 2)
        client.discover()
        with patch.object(websocket, 'call', return_value={}), \
                patch.object(websocket, 'receive', return_value={
                    'method': 'Target.dispatchMessageFromTarget', 'params': {'targetId': 'page-1',
                    'message': json.dumps({'id': 1, 'result': {'value': 'later'}})}}):
            self.assertEqual(client.call('Runtime.evaluate'), {'value': 'later'})

    def test_inner_error_is_not_success(self):
        websocket = WebSocket()
        client = inspector.PageClient(websocket, time.monotonic() + 2)
        client.discover()
        with patch.object(websocket, 'call', return_value={}), \
                patch.object(websocket, 'receive', return_value={
                    'method': 'Target.dispatchMessageFromTarget', 'params': {'targetId': 'page-1',
                    'message': json.dumps({'id': 1, 'error': {'message': 'unsupported'}})}}):
            with self.assertRaisesRegex(RuntimeError, 'unsupported'):
                client.call('Runtime.evaluate')

    def test_evaluation_exception_closes_connection(self):
        opener = Mock()
        opener.open.return_value.__enter__ = Mock(return_value=SimpleNamespace(read=lambda size:
            b'<tr><div class="targeturl">http://127.0.0.1:8080/player</div>'
            b'<input onclick="/socket/1/2/WebPage"></tr>'))
        opener.open.return_value.__exit__ = Mock(return_value=False)
        websocket = Mock()
        transport = SimpleNamespace(WebSocket=Mock(return_value=websocket))
        client = Mock()
        client.call.side_effect = [{}, {'wasThrown': True, 'result': {'description': 'failed'}}]
        with patch('sys.argv', ['inspect-wpe.py']), \
                patch.object(inspector, 'build_opener', return_value=opener), \
                patch.object(inspector.importlib.util, 'module_from_spec', return_value=transport), \
                patch.object(inspector.importlib.util, 'spec_from_file_location', return_value=Mock()), \
                patch.object(inspector, 'PageClient', return_value=client), \
                patch('builtins.print') as output:
            with self.assertRaisesRegex(RuntimeError, 'Page observation failed'):
                inspector.main()
        websocket.socket.close.assert_called_once()
        output.assert_not_called()
        self.assertIn('deadline', transport.WebSocket.call_args.kwargs)


if __name__ == '__main__':
    unittest.main()

import importlib.util
import json
from pathlib import Path
import time
from types import SimpleNamespace
import unittest
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

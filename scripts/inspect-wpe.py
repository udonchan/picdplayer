#!/usr/bin/env python3
"""Bounded, loopback-only WebKit inspector observation; no service control.

WPE 2.54 HTTP /socket routes to a WebPage target, which multiplexes page
commands via Target.sendMessageToTarget, unlike a Chromium page endpoint.
"""
import argparse
import base64
from html.parser import HTMLParser
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import time
import zlib
from urllib.request import ProxyHandler, build_opener


class Targets(HTMLParser):
    def __init__(self):
        super().__init__()
        self.targets = []
        self.url = ''
        self.in_url = False

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if tag == 'tr':
            self.url = ''
        if tag == 'div' and attrs.get('class') == 'targeturl':
            self.in_url = True
        if tag == 'input':
            match = re.search(r'/socket/(\d+)/(\d+)/WebPage', attrs.get('onclick', ''))
            if match:
                self.targets.append({'url': self.url, 'path': match.group(0)})

    def handle_endtag(self, tag):
        if tag == 'div':
            self.in_url = False

    def handle_data(self, data):
        if self.in_url:
            self.url += data


class PageClient:
    def __init__(self, websocket, deadline):
        self.websocket = websocket
        self.deadline = deadline
        self.target = None
        self.inner_id = 0
        self.responses = {}

    def event(self, value):
        if value.get('method') == 'Target.targetCreated':
            info = value['params']['targetInfo']
            if info['type'] == 'page' and not info.get('isProvisional'):
                if info.get('isPaused'):
                    raise RuntimeError('Page is paused; refusing to change debugger execution state')
                self.target = info['targetId']
        elif value.get('method') == 'Target.dispatchMessageFromTarget':
            if value['params']['targetId'] != self.target:
                return
            message = json.loads(value['params']['message'])
            if 'id' in message:
                self.responses[message['id']] = message
        elif value.get('method') == 'Target.targetDestroyed' and value['params']['targetId'] == self.target:
            raise RuntimeError('Inspected page target was destroyed')

    def receive(self):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('Inspector connection deadline expired')
        self.websocket.socket.settimeout(min(5, remaining))
        self.event(self.websocket.receive())

    def discover(self):
        while self.target is None:
            self.receive()

    def call(self, method, params=None):
        self.inner_id += 1
        identifier = self.inner_id
        message = {'id': identifier, 'method': method, 'params': params or {}}
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('Inspector connection deadline expired')
        self.websocket.socket.settimeout(min(5, remaining))
        self.websocket.call('Target.sendMessageToTarget',
            {'targetId': self.target, 'message': json.dumps(message)}, on_event=self.event)
        while identifier not in self.responses:
            self.receive()
        response = self.responses.pop(identifier)
        if 'error' in response:
            raise RuntimeError(response['error'])
        return response.get('result', {})


def save_screenshot(client, observation, path):
    try:
        viewport = json.loads(observation['result']['value'])
        width, height = viewport['width'], viewport['height']
    except (KeyError, TypeError, ValueError) as error:
        raise RuntimeError('Invalid viewport observation') from error
    if (type(width) is not int or type(height) is not int or
            not 0 < width <= 3840 or not 0 < height <= 3840 or width * height > 3840 * 2160):
        raise RuntimeError('Viewport exceeds screenshot limits')
    result = client.call('Page.snapshotRect', {
        'x': 0, 'y': 0, 'width': width, 'height': height, 'coordinateSystem': 'Viewport'})
    value = result.get('dataURL') if isinstance(result, dict) else None
    prefix = 'data:image/png;base64,'
    limit = 8 * 1024 * 1024
    if not isinstance(value, str) or not value.startswith(prefix):
        raise RuntimeError('Snapshot did not return a PNG data URL')
    encoded = value[len(prefix):]
    if len(encoded) > ((limit + 2) // 3) * 4:
        raise RuntimeError('Snapshot exceeds PNG byte limit')
    try:
        png = base64.b64decode(encoded, validate=True)
    except ValueError as error:
        raise RuntimeError('Invalid snapshot base64') from error
    if (len(png) > limit or len(png) < 45 or not png.startswith(b'\x89PNG\r\n\x1a\n') or
            png[8:16] != b'\x00\x00\x00\x0dIHDR' or
            zlib.crc32(png[12:29]) != struct.unpack('!I', png[29:33])[0] or
            not png.endswith(b'\x00\x00\x00\x00IEND\xaeB`\x82')):
        raise RuntimeError('Invalid snapshot PNG envelope')
    pixel_width, pixel_height = struct.unpack('!II', png[16:24])
    if (not 0 < pixel_width <= 7680 or not 0 < pixel_height <= 7680 or
            pixel_width * pixel_height > 7680 * 4320):
        raise RuntimeError('PNG dimensions exceed screenshot limits')
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, 'wb') as output:
            output.write(png)
    except BaseException:
        path.unlink()
        raise
    return {'path': str(path), 'width': pixel_width, 'height': pixel_height, 'bytes': len(png)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=19223, help='Local SSH-forwarded inspector port')
    parser.add_argument('--target-url', default='http://127.0.0.1:8080/player')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--list', action='store_true')
    mode.add_argument('--screenshot', type=Path, metavar='PATH', help='Save current WPE viewport as a new PNG')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error('port must be 1..65535')
    if args.screenshot and (args.screenshot.exists() or args.screenshot.is_symlink()):
        parser.error('screenshot path already exists; choose a new filename')
    if args.screenshot and not args.screenshot.parent.is_dir():
        parser.error('screenshot parent directory must already exist')
    opener = build_opener(ProxyHandler({}))
    with opener.open(f'http://127.0.0.1:{args.port}/', timeout=5) as response:
        html = response.read(512 * 1024 + 1)
    if len(html) > 512 * 1024:
        raise RuntimeError('Inspector listing exceeds size limit')
    targets = Targets()
    targets.feed(html.decode('utf-8'))
    if args.list:
        print(json.dumps(targets.targets))
        return
    matches = [target for target in targets.targets if target['url'] == args.target_url]
    if len(matches) != 1:
        raise RuntimeError('Expected exactly one matching target; use --list')
    spec = importlib.util.spec_from_file_location('kiosk_transport', Path(__file__).with_name('measure-kiosk-cdp.py'))
    transport = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(transport)
    started = time.monotonic()
    options = {'deadline': started + 25}
    if args.screenshot:
        options['maximum_message_bytes'] = 16 * 1024 * 1024
    websocket = transport.WebSocket(f"ws://127.0.0.1:{args.port}{matches[0]['path']}", **options)
    try:
        client = PageClient(websocket, started + 25)
        client.discover()
        client.call('Runtime.enable')
        observation = client.call('Runtime.evaluate', {'expression':
            "JSON.stringify({title:document.title,ready:document.readyState,width:innerWidth,height:innerHeight,"
             "connection:document.getElementById('connection')?.dataset.state})", 'returnByValue': True})
        if observation.get('wasThrown'):
            raise RuntimeError(f"Page observation failed: {observation.get('result', {})}")
        result = {'connected_seconds': round(time.monotonic() - started, 3), 'observation': observation}
        if args.screenshot:
            result['screenshot'] = save_screenshot(client, observation, args.screenshot)
        print(json.dumps(result))
    finally:
        websocket.socket.close()


if __name__ == '__main__':
    main()

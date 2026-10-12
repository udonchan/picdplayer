#!/usr/bin/env python3
"""Bounded, loopback-only WebKit inspector observation; no service control.

WPE 2.54 HTTP /socket routes to a WebPage target, which multiplexes page
commands via Target.sendMessageToTarget, unlike a Chromium page endpoint.
"""
import argparse
from html.parser import HTMLParser
import importlib.util
import json
from pathlib import Path
import re
import time
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=19223, help='Local SSH-forwarded inspector port')
    parser.add_argument('--target-url', default='http://127.0.0.1:8080/player')
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error('port must be 1..65535')
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
    websocket = transport.WebSocket(f"ws://127.0.0.1:{args.port}{matches[0]['path']}", deadline=started + 25)
    try:
        client = PageClient(websocket, started + 25)
        client.discover()
        client.call('Runtime.enable')
        observation = client.call('Runtime.evaluate', {'expression':
            "JSON.stringify({title:document.title,ready:document.readyState,width:innerWidth,height:innerHeight,"
             "connection:document.getElementById('connection')?.dataset.state})", 'returnByValue': True})
        if observation.get('wasThrown'):
            raise RuntimeError(f"Page observation failed: {observation.get('result', {})}")
        print(json.dumps({'connected_seconds': round(time.monotonic() - started, 3), 'observation': observation}))
    finally:
        websocket.socket.close()


if __name__ == '__main__':
    main()

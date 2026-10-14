"""Static contract for the bounded WPE SDK workflow, not a general YAML parser."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
PRODUCER = (ROOT / '.github/workflows/wpe-sdk.yml').read_text()
CONSUMER = (ROOT / '.github/workflows/ci.yml').read_text()
HEADER = """name: WPE SDK

on:
  workflow_dispatch:
  push:
    branches: [master]
    paths:
      - .github/workflows/wpe-sdk.yml
      - .github/workflows/ci.yml
      - scripts/wpe-source-*
      - scripts/build-wpe-runtime.sh

permissions:
  contents: read

concurrency:
  group: wpe-sdk-master
  cancel-in-progress: false

jobs:
  build-sdk:
    if: github.ref == 'refs/heads/master'
    runs-on: ubuntu-24.04-arm
    timeout-minutes: 180
    steps:
"""


def steps(text):
    # Only six-space step starts and their eight/ten-space fields are inspected.
    return re.split(r'^      - ', text.partition('    steps:\n')[2], flags=re.MULTILINE)[1:]


class Contract(unittest.TestCase):
    def check_contract(self, producer, consumer=CONSUMER):
        header, separator, _ = producer.partition('    steps:\n')
        self.assertEqual(header + separator, HEADER)
        cache, = [step for step in steps(consumer) if '\n        uses: actions/cache@v4\n' in step]
        path = re.search(r'^          path: (.+)$', cache, re.MULTILINE).group(1)
        key = re.search(r'^          key: (.+)$', cache, re.MULTILINE).group(1)
        # Consumer owns the recipe key; compare the exact expression, including spacing.
        actual = [re.sub(r'^name: [^\n]+\n        ', '', step) for step in steps(producer)]
        self.assertEqual(actual, [
            'uses: actions/checkout@v6\n        with:\n          persist-credentials: false\n',
            'uses: actions/cache/restore@v4\n        id: sdk-cache\n        with:\n'
            f'          path: {path}\n          key: {key}\n',
            'run: bash scripts/build-wpe-runtime.sh\n',
            "uses: actions/cache/save@v4\n        if: steps.sdk-cache.outputs.cache-hit != 'true'\n"
            f'        with:\n          path: {path}\n'
            '          key: ${{ steps.sdk-cache.outputs.cache-primary-key }}\n',
        ])

    def test_repository_contract(self):
        self.check_contract(PRODUCER)

    def test_step_names_do_not_change_contract(self):
        self.check_contract(PRODUCER.replace('Build and validate WPE SDK', 'Validate on hit or miss'))

    def test_consumer_is_cache_source_of_truth(self):
        for old, new in [('wpe-trixie-arm64-', 'different-recipe-'),
                         ('build-wpe-runtime/', 'different-sdk/')]:
            with self.subTest(field=old), self.assertRaises(AssertionError):
                self.check_contract(PRODUCER, CONSUMER.replace(old, new))
            self.check_contract(PRODUCER.replace(old, new), CONSUMER.replace(old, new))

    def test_unsafe_producer_mutations(self):
        build = '      - name: Build and validate WPE SDK\n        run: bash scripts/build-wpe-runtime.sh\n'
        save = PRODUCER[PRODUCER.index('      - name: Save source-built WPE SDK'):]
        mutations = [
            ('wpe-trixie-arm64-', 'wrong-recipe-'), ('build-wpe-runtime/', 'wrong-sdk/'),
            ("    if: github.ref == 'refs/heads/master'\n", ''),
            ('  workflow_dispatch:\n', '  pull_request:\n'),
            ('  workflow_dispatch:\n', '  pull_request_target:\n'),
            ('  workflow_dispatch:\n', '  schedule:\n'),
            ('branches: [master]', 'branches: [feature/wpe-sdk-cache]'),
            ('contents: read', 'contents: write'), ('cancel-in-progress: false', 'cancel-in-progress: true'),
            (build, ''), (save, ''), (build + save, save + build),
            ("        if: steps.sdk-cache.outputs.cache-hit != 'true'\n", ''),
            ('steps.sdk-cache.outputs.cache-primary-key', 'steps.sdk-cache.outputs.cache-matched-key'),
            ('        run: bash scripts/build-wpe-runtime.sh\n',
             "        if: steps.sdk-cache.outputs.cache-hit != 'true'\n        run: bash scripts/build-wpe-runtime.sh\n"),
            ('        run: bash scripts/build-wpe-runtime.sh', '        run: bash scripts/build-container.sh'),
            ('    steps:\n', '    steps:\n      - run: gh release create example\n'),
        ]
        for old, new in mutations:
            with self.subTest(mutation=old), self.assertRaises(AssertionError):
                self.check_contract(PRODUCER.replace(old, new))
            self.check_contract(PRODUCER)


if __name__ == '__main__':
    unittest.main()

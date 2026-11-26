"""Documentation contract only; does not certify cross-device hardware support."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'docs'


class PortabilityPolicy(unittest.TestCase):
    def test_agent_rule_declares_portability_contract(self):
        text = (ROOT / 'AGENTS.md').read_text()
        self.assertLess(text.index('SPX-PORTABLE-SQ1-SQ2'), text.index('## 构建'))
        for term in ['任意 SQ1/SQ2', '禁止单机身份绑定', 'docs/DEVELOPMENT.md']:
            self.assertIn(term, text)

    def test_development_contract_has_all_six_sections(self):
        en = (DOCS / 'DEVELOPMENT.md').read_text()
        zh = (DOCS / 'DEVELOPMENT.zh-CN.md').read_text()
        for number in range(1, 7):
            self.assertEqual(en.count('## PORT-%02d:' % number), 1)
            self.assertEqual(zh.count('## PORT-%02d：' % number), 1)
        for term in ['single standard image', 'Serial numbers', 'MAC', 'IP/SSH', 'UUID/ESP', 'phandle',
                     'FSG', "target's own storage", 'screen_only', 'prevent mis-writes', 'Untested']:
            self.assertIn(term, en)
        for term in ['同一份镜像', '不允许', '序列号', 'MAC', 'IP/SSH', 'UUID/ESP', 'phandle',
                     'FSG', '目标自身', 'screen_only', 'nohibernate', '防止误写', '未测']:
            self.assertIn(term, zh)

    def test_build_guidance_references_portability_contract(self):
        for name in ['BUILD.md', 'STANDARD-BUILD.md']:
            self.assertIn('DEVELOPMENT.md', (DOCS / name).read_text())

    def test_todo_lists_open_portability_work(self):
        todo_en = (DOCS / 'TODO.md').read_text()
        todo_zh = (DOCS / 'TODO.zh-CN.md').read_text()
        self.assertIn('## P0: Generic SQ1/SQ2 Delivery Remediation', todo_en)
        self.assertIn('## Delivery Acceptance', todo_en)
        self.assertIn('## P0：任意 SQ1/SQ2 通用交付整改', todo_zh)
        self.assertIn('## 交付验收', todo_zh)

    def test_historical_records_are_not_committed_documents(self):
        self.assertFalse((DOCS / 'HANDOFF.md').exists())
        for path in DOCS.glob('*.md'):
            self.assertIsNone(
                re.match(r'.*-(19|20)\d{6}\.md$', path.name),
                'dated history doc committed: %s' % path.name)

    def test_relative_doc_links_resolve(self):
        missing = []
        for path in [ROOT / 'README.md', ROOT / 'README.zh-CN.md', ROOT / 'AGENTS.md'] + sorted(DOCS.glob('*.md')):
            for target in re.findall(r'\]\(([^)#]+\.md)\)', path.read_text()):
                if target.startswith(('http://', 'https://')):
                    continue
                resolved = (path.parent / target).resolve()
                if not resolved.exists():
                    missing.append('%s -> %s' % (path.name, target))
        self.assertEqual([], missing)


if __name__ == '__main__':
    unittest.main()
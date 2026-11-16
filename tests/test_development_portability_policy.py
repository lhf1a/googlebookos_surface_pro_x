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
        text = (DOCS / 'DEVELOPMENT.md').read_text()
        for number in range(1, 7):
            self.assertEqual(text.count('## PORT-%02d：' % number), 1)
        for term in ['同一份镜像', '不允许', '序列号', 'MAC', 'IP/SSH', 'UUID/ESP', 'phandle',
                     'FSG', '目标自身', 'screen_only/nohibernate', '防误写', '未测']:
            self.assertIn(term, text)

    def test_build_guidance_references_portability_contract(self):
        for name in ['BUILD.md', 'STANDARD-BUILD.md']:
            self.assertIn('DEVELOPMENT.md', (DOCS / name).read_text())

    def test_todo_lists_open_portability_work(self):
        todo = (DOCS / 'TODO.md').read_text()
        self.assertIn('P0：任意 SQ1/SQ2 通用交付整改', todo)
        self.assertIn('待审计/整改/验收', todo)

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
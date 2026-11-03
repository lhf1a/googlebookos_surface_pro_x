"""Documentation contract only; does not certify cross-device hardware support."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]


class PortabilityPolicy(unittest.TestCase):
    def test_agent_rule_is_authoritative_before_historical_unit_rules(self):
        text=(ROOT/'AGENTS.md').read_text()
        self.assertLess(text.index('SPX-PORTABLE-SQ1-SQ2'),text.index('- Default build/menu'))
        for term in ['任意 SQ1/SQ2','禁止单机身份绑定','docs/DEVELOPMENT.md','本节覆盖下方历史单机实验约定']:
            self.assertIn(term,text)

    def test_development_contract_has_all_six_sections(self):
        text=(ROOT/'docs/DEVELOPMENT.md').read_text()
        for number in range(1,7):self.assertEqual(text.count('## PORT-%02d：'%number),1)
        for term in ['同一份镜像','不允许','序列号','MAC','IP/SSH','UUID/ESP','phandle',
                     'FSG','目标自身','screen_only/nohibernate','防误写','未测']:
            self.assertIn(term,text)

    def test_current_build_guidance_has_no_old_single_sq2_admission(self):
        text=(ROOT/'docs/STANDARD-BUILD.md').read_text()
        self.assertNotIn('仅原绑定 SQ2 可激活',text)
        for name in ['BUILD.md','HANDOFF.md','STANDARD-BUILD.md','PRIVATE-BUILD.md']:
            self.assertIn('DEVELOPMENT.md',(ROOT/'docs'/name).read_text())

    def test_migration_limits_are_explicit_not_hardware_certification(self):
        todo=(ROOT/'docs/TODO.md').read_text();private=(ROOT/'docs/PRIVATE-BUILD.md').read_text()
        self.assertIn('P0：任意 SQ1/SQ2 通用交付整改',todo)
        self.assertIn('待审计/整改/验收',todo)
        self.assertIn('不代表这些实现已修正',private)
        self.assertIn('不得直接作为通用交付',private)


if __name__=='__main__':unittest.main()

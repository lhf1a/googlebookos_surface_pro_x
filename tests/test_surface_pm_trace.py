import importlib.util
from pathlib import Path
import tempfile
import unittest

R = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pm_trace', R/'scripts/analyze-surface-pm-trace.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def event(pid, cpu, time, kind, data):
    return f'worker-long-name-{pid} [{cpu:03}] ..... {time}: {kind}: {data}\n'


class PmTrace(unittest.TestCase):
    def test_async_tasks_migrate_and_keep_phase_and_error(self):
        trace = '# entries-in-buffer/entries-written: 8/8\n'
        trace += event(1,0,'1.000000','suspend_resume','dpm_suspend[2] begin')
        trace += event(11,0,'1.000001','device_pm_callback_start','test_drv unitA, parent: root, [suspend]')
        trace += event(12,1,'1.000002','device_pm_callback_start','test_drv unitB, parent: root, driver [suspend]')
        trace += event(12,2,'1.000010','device_pm_callback_end','test_drv unitB, err=-110')
        trace += event(11,3,'1.000011','device_pm_callback_end','test_drv unitA, err=0')
        trace += event(1,0,'1.000020','suspend_resume','dpm_suspend[2] end')
        records, summary = m.analyze(trace)
        self.assertEqual([r['duration_us'] for r in records], [10,8])
        self.assertEqual([r['phase'] for r in records], ['dpm_suspend']*2)
        self.assertEqual(summary['callbacks'], {'returned':2})
        self.assertEqual(summary['errors'][0]['device'], 'unitB')
        self.assertEqual(records[1]['callback'], 'driver')
        self.assertFalse(summary['recorded_gaps'])

    def test_blank_driver_repeated_device_and_missing_boundaries(self):
        trace = '# entries-in-buffer/entries-written: 5/9\n'
        trace += event(1,0,'1.000001','device_pm_callback_start',' unitA, parent: none, [suspend]')
        trace += event(1,1,'1.000002','device_pm_callback_end',' unitA, err=0')
        trace += event(1,0,'1.000003','device_pm_callback_start',' unitA, parent: none, [resume]')
        trace += event(2,0,'1.000004','device_pm_callback_end','drv other, err=0')
        records, summary = m.analyze(trace)
        self.assertEqual([r['status'] for r in records], ['returned','end_missing','start_missing'])
        self.assertEqual(records[0]['driver'], '')
        self.assertTrue(summary['recorded_gaps'])
        self.assertEqual(summary['unique_driver_device_pairs'], 2)

    def test_unknown_formats_reversed_time_and_missing_header(self):
        self.assertFalse(m.analyze('')[1]['header_present'])
        with self.assertRaises(ValueError):
            m.analyze(event(1,0,'1.0','device_pm_callback_start','changed kernel format'))
        with self.assertRaises(ValueError):
            m.analyze(event(1,0,'2.0','device_pm_callback_start','drv A, parent: root, [suspend]')+
                      event(1,0,'1.0','device_pm_callback_end','drv A, err=0'))

    def test_reports_are_fresh_and_keep_all_records(self):
        trace = event(1,0,'1.0','device_pm_callback_start','drv A, parent: root, [suspend]')
        with tempfile.TemporaryDirectory() as d:
            source = Path(d)/'trace'
            source.write_text(trace)
            output = Path(d)/'report'
            m.write_report(source,output)
            self.assertIn('end_missing',(output/'callbacks.csv').read_text())
            self.assertIn('missing_boundaries',(output/'components.csv').read_text())
            with self.assertRaises(FileExistsError):
                m.write_report(source,output)

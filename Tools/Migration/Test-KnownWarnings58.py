"""Regression checks for warning triage, without starting Unreal."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('report', Path(__file__).with_name('Report-KnownWarnings58.py'))
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class WarningReportTests(unittest.TestCase):
    def test_classification_and_original_preservation(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'uat.log'
            original = ('LogTest: Warning: Known\nLogInit: Display: LogTest: Warning: Known\n'
                        'LogInit: Display: Success - 0 error(s), 1 warning(s)\n'
                        'AutomationTool exiting with ExitCode=0\n')
            path.write_text(original, encoding='utf-8')
            data = report.scan(path)
            baseline = {'warnings': [{'phase': 'cook', 'message': 'LogTest: Warning: Known', 'count': 1}]}
            rows, alert = report.compare(data, baseline)
            self.assertFalse(alert)
            self.assertEqual(rows[0]['actual'], 1)
            self.assertEqual(path.read_text(encoding='utf-8'), original)
            for injection, expected in [('LogTest: Warning: Known\n', 'AUMENTO'),
                                        ('LogTest: Warning: Changed\n', 'NUEVA_O_CAMBIADA')]:
                path.write_text(injection + original, encoding='utf-8')
                rows, alert = report.compare(report.scan(path), baseline)
                self.assertTrue(alert)
                self.assertIn(expected, [x['status'] for x in rows])
            for injection in ('LogTest: Error: Failure\n', 'Condition failed\n'):
                path.write_text(injection + original, encoding='utf-8')
                self.assertTrue(report.compare(report.scan(path), baseline)[1])
            path.write_text(original.replace('AutomationTool exiting with ExitCode=0', ''), encoding='utf-8')
            self.assertTrue(report.compare(report.scan(path), baseline)[1])
            path.write_text('AutomationTool exiting with ExitCode=0\n', encoding='utf-16')
            rows, alert = report.compare(report.scan(path), baseline)
            self.assertFalse(alert)
            self.assertEqual(rows[0]['status'], 'NO_OBSERVADA')


if __name__ == '__main__':
    unittest.main()

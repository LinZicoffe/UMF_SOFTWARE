"""Runner I/O contract tests; mocks here do not verify production firmware.

Run separately with unittest discover -s tests/firmware -p test_runner_errors.py.
The real CPU execution is covered by run.py, not by the Image mock below.
"""
from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import run


class ReportExitTests(unittest.TestCase):
    def call_runner(self, write_error=None):
        (run.HERE / "build").mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=run.HERE / "build") as directory:
            output = io.StringIO()
            with (patch.object(run.sys, "argv", ["run.py", "--group", "smoke", "--output", directory]),
                  patch.object(run, "build", return_value=(Path("unused.elf"), {})),
                  patch.object(run, "command_run", return_value=SimpleNamespace(stdout="test-revision")),
                  patch.object(run, "Image") as image,
                  patch.object(Path, "write_text", side_effect=write_error),
                  redirect_stdout(output), redirect_stderr(output)):
                image.return_value.symbols = {"test_smoke_itoa": 0}
                image.return_value.origins = {}
                image.return_value.execute.return_value = {
                    "name": "test_smoke_itoa", "status": "PASS", "reason": None
                }
                code = run.main()
            return code, output.getvalue()

    def test_successful_report_preserves_success_exit(self):
        code, output = self.call_runner()
        self.assertEqual(code, 0, output)

    def test_report_write_failure_returns_environment_error(self):
        code, output = self.call_runner(PermissionError("injected report write denial"))
        self.assertIn("cannot save report", output)
        self.assertEqual(code, 2, output)


if __name__ == "__main__":
    unittest.main()

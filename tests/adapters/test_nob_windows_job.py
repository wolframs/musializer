import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class NobWindowsJobLaunchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (ROOT / "thirdparty" / "nob.h").read_text(encoding="utf-8")

    def test_job_option_is_windows_only(self) -> None:
        options = re.search(
            r"typedef struct \{.*?HANDLE win32_job_object;.*?\} Nob_Cmd_Opt;",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(options)
        self.assertIn("#ifdef _WIN32", options.group(0))

    def test_job_assignment_happens_before_child_resume(self) -> None:
        declarations = [
            match.start()
            for match in re.finditer(
                r"static Nob_Proc nob__cmd_start_process\(", self.source
            )
        ]
        self.assertGreaterEqual(len(declarations), 2)
        implementation = declarations[-1]
        body = self.source[
            implementation : self.source.index("\n#else", implementation)
        ]

        self.assertIn("assign_to_job ? CREATE_SUSPENDED : 0", body)
        create = body.index("CreateProcessA(")
        assign = body.index("AssignProcessToJobObject(")
        resume = body.index("ResumeThread(")
        self.assertLess(create, assign)
        self.assertLess(assign, resume)

        assign_failure = body[assign:resume]
        self.assertIn("TerminateProcess(", assign_failure)
        self.assertIn("WaitForSingleObject(", assign_failure)
        self.assertIn("CloseHandle(piProcInfo.hThread)", assign_failure)
        self.assertIn("CloseHandle(piProcInfo.hProcess)", assign_failure)


if __name__ == "__main__":
    unittest.main()

"""Exercise the actual dependency patch runner without fetching or compiling enkiTS."""

import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEPENDENCY = ROOT / "engine/job/third_party/enkiTS"
PATCH = DEPENDENCY / "completion_ordering.patch"
RUNNER = DEPENDENCY / "apply_completion_ordering.cmake"


def patch_regions():
    """Reconstruct both sides of the real hunks, padding unrelated source lines."""
    before, after = {}, {}
    name = None
    for line in PATCH.read_text().splitlines(keepends=True):
        if line.startswith("--- a/"):
            name = line[6:].strip()
            before[name], after[name] = [], []
        elif line.startswith("+++ b/"):
            continue
        elif line.startswith("@@"):
            match = re.match(r"@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@", line)
            if match is None:
                raise ValueError(f"unsupported hunk: {line}")
            for contents, start in ((before, match[1]), (after, match[2])):
                while len(contents[name]) < int(start) - 1:
                    contents[name].append("// unrelated source\n")
        elif name is not None and line[0] in " +-":
            if line[0] in " -":
                before[name].append(line[1:])
            if line[0] in " +":
                after[name].append(line[1:])
    return (
        {name: "".join(lines) for name, lines in before.items()},
        {name: "".join(lines) for name, lines in after.items()},
    )


@unittest.skipUnless(shutil.which("cmake") and shutil.which("git"), "requires cmake and git")
class EnkitsCompletionPatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="enkits-completion-")
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "dependency source"
        self.before, self.after = patch_regions()
        self.write_sources(self.before)

    def write_sources(self, contents):
        for name, text in contents.items():
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)

    def read_sources(self):
        return {name: (self.source / name).read_text() for name in self.before}

    def run_patch(self, source=None, env=None):
        return subprocess.run(
            ["cmake", f"-DENKITS_SOURCE_DIR={source or self.source}", "-P", str(RUNNER)],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
            env=env,
        )

    def test_applies_all_hunks_and_reapplication_preserves_files(self):
        result = self.run_patch()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.read_sources(), self.after)
        timestamps = {name: (self.source / name).stat().st_mtime_ns for name in self.before}
        result = self.run_patch()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.read_sources(), self.after)
        self.assertEqual(
            timestamps, {name: (self.source / name).stat().st_mtime_ns for name in self.before}
        )

    def test_partial_patch_fails_without_mutating_either_file(self):
        self.write_sources({"src/TaskScheduler.h": self.after["src/TaskScheduler.h"]})
        original = self.read_sources()
        result = self.run_patch()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("incompatible", result.stderr)
        self.assertEqual(self.read_sources(), original)

    def test_conflicting_hunk_fails_without_partial_application(self):
        self.write_sources(
            {
                "src/TaskScheduler.h": self.before["src/TaskScheduler.h"].replace(
                    "m_RunningCount.fetch_sub", "differentCounter.fetch_sub"
                )
            }
        )
        original = self.read_sources()
        result = self.run_patch()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("incompatible", result.stderr)
        self.assertEqual(self.read_sources(), original)
        self.assertFalse(list(self.source.rglob("*.rej")))

    def test_missing_source_fails(self):
        result = self.run_patch(self.source / "missing")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires ENKITS_SOURCE_DIR", result.stderr)

    def test_archive_inside_parent_repository_only_patches_its_own_files(self):
        parent = Path(self.temp.name)
        subprocess.run(["git", "init", str(parent)], capture_output=True, check=True, timeout=15)
        for name in self.before:
            path = parent / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("parent checkout must remain unchanged\n")
        for inherited in (False, True):
            with self.subTest(inherited_git_environment=inherited):
                self.write_sources(self.before)
                env = os.environ.copy()
                if inherited:
                    other = parent / "other checkout"
                    subprocess.run(
                        ["git", "init", str(other)], capture_output=True, check=True, timeout=15
                    )
                    env.update(GIT_DIR=str(other / ".git"), GIT_WORK_TREE=str(other))
                result = self.run_patch(env=env)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(self.read_sources(), self.after)
                for name in self.before:
                    self.assertEqual(
                        (parent / name).read_text(), "parent checkout must remain unchanged\n"
                    )
                self.assertFalse((self.source / ".git").exists())

    def test_configure_revision_transitions_do_not_carry_default_patch(self):
        temp = Path(self.temp.name)
        upstream = temp / "upstream"
        upstream.mkdir()
        env = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
        subprocess.run(
            ["git", "init", str(temp)], capture_output=True, check=True, timeout=15, env=env
        )
        for name in self.before:
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("enclosing engine checkout sentinel\n")

        def git(*args):
            return subprocess.run(
                ["git", "-C", str(upstream), *args],
                capture_output=True,
                text=True,
                check=True,
                timeout=15,
                env=env,
            ).stdout.strip()

        def revision(contents, label):
            for name, text in contents.items():
                path = upstream / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            git("add", ".")
            git(
                "-c",
                "user.name=Fixture",
                "-c",
                "user.email=fixture@example.invalid",
                "commit",
                "-m",
                label,
            )
            git("tag", label)
            return label

        git("init")
        (upstream / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.15)\n"
            "project(enki_fixture LANGUAGES CXX)\n"
            "add_library(enkiTS INTERFACE)\n"
        )
        default = revision(self.before, "default")
        archive = temp / "enki-default.tar.gz"
        git(
            "archive", "--format=tar.gz", "--prefix=enkits-fixture/", default, f"--output={archive}"
        )
        archive_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
        incompatible = dict(self.before, **{"src/TaskScheduler.h": "// custom header\n"})
        partial = dict(self.before, **{"src/TaskScheduler.h": self.after["src/TaskScheduler.h"]})
        custom_revisions = [
            (revision(incompatible, "custom-incompatible"), incompatible),
            (revision(self.after, "custom-already-fixed"), self.after),
            (revision(partial, "custom-partial"), partial),
        ]

        project = temp / "project"
        wrapper = project / "dependency"
        shutil.copytree(DEPENDENCY, wrapper)
        cmake_file = wrapper / "CMakeLists.txt"
        wrapper_source = cmake_file.read_text().replace(
            "https://github.com/dougbinks/enkiTS.git", upstream.as_uri()
        )
        wrapper_source = re.sub(
            r"https://github.com/dougbinks/enkiTS/archive/[a-f0-9]+\.tar\.gz",
            archive.as_uri(),
            wrapper_source,
        )
        cmake_file.write_text(
            re.sub(r"SHA256=[a-f0-9]{64}", f"SHA256={archive_hash}", wrapper_source)
        )
        (project / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.15)\n"
            "project(completion_transition LANGUAGES CXX)\n"
            "add_subdirectory(dependency)\n"
        )
        build = temp / "build"
        populated = temp / "populated"

        def configure(pin):
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(project),
                    "-B",
                    str(build),
                    f"-DFETCHCONTENT_BASE_DIR={populated}",
                    f"-DIR_ENKITS_GIT_REF={pin}",
                ],
                capture_output=True,
                text=True,
                timeout=45,
                check=False,
                env=env,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name in self.before:
                self.assertEqual((temp / name).read_text(), "enclosing engine checkout sentinel\n")

        def verify_sources(directory, expected):
            self.assertEqual({name: (directory / name).read_text() for name in expected}, expected)

        configure("")
        default_source = populated / "enkits_src-src"
        verify_sources(default_source, self.after)
        self.assertFalse((default_source / ".git").exists())
        for pin, contents in custom_revisions:
            configure(pin)
            digest = hashlib.sha256(pin.encode()).hexdigest()
            custom_source = populated / "enkits-revisions" / digest / "enkits_src-src"
            verify_sources(custom_source, contents)
            verify_sources(default_source, self.after)
            configure("")
            verify_sources(default_source, self.after)
            verify_sources(custom_source, contents)

    def test_backport_contains_only_all_ten_atomic_order_changes(self):
        lines = PATCH.read_text().splitlines()
        removed = [
            line[1:] for line in lines if line.startswith("-") and not line.startswith("---")
        ]
        added = [line[1:] for line in lines if line.startswith("+") and not line.startswith("+++")]
        self.assertEqual(len(removed), 10)
        self.assertEqual(len(added), 10)
        for old, new in zip(removed, added, strict=True):
            self.assertEqual(
                re.sub(r"memory_order_(acquire|release)", "memory_order_acq_rel", old), new
            )
        self.assertEqual(sum("m_RunningCount.fetch_sub" in line for line in added), 6)
        self.assertEqual(sum("m_WaitingForTaskCount.fetch_" in line for line in added), 2)
        self.assertEqual(
            sum("m_NumThreadsWaitingForTaskCompletion.fetch_" in line for line in added), 2
        )


if __name__ == "__main__":
    unittest.main()

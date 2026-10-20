"""Test runner module.

Handles building, executing, and exit-code propagation for test projects.
Used by Build.py when -test, -benchmark, or -testall flags are provided.
"""

import os
import subprocess
import ProjectManager as ProjectManager
import Config


def RunTestProject(proj: ProjectManager.Project, extra_args: list[str] | None = None) -> int:
    """Build and run a single test project. Returns the process exit code.

    Assumes the project has already been built successfully.
    """
    output = proj.GetOutput()
    if not os.path.exists(output):
        print(f"Error: Test executable '{output}' not found.")
        return 2

    cmd = [output]
    if extra_args:
        cmd.extend(extra_args)

    result = subprocess.run(cmd, cwd=proj.folder, text=True)
    return result.returncode


def FindTestProjects() -> list[ProjectManager.Project]:
    """Return all loaded projects that are unit test executables.

    Priority:
    1. Projects with ``isUnitTestProject = True`` (explicit flag).
    2. Fallback heuristic: project is an EXECUTABLE and its name contains
       'testing' (case-insensitive) or ends with 'test'.
    """
    test_projects: list[ProjectManager.Project] = []

    # Explicitly flagged projects take priority.
    for proj in ProjectManager.projects.values():
        if proj.isUnitTestProject:
            test_projects.append(proj)

    # Fallback heuristic for projects that haven't been flagged yet.
    if not test_projects:
        for proj in ProjectManager.projects.values():
            if proj.buildType != Config.BuildType.EXECUTABLE:
                continue
            name = proj.projectName.lower()
            if "testing" in name or name.endswith("test"):
                test_projects.append(proj)

    # Sort by name for deterministic ordering.
    test_projects.sort(key=lambda p: p.projectName)
    return test_projects


def RunAllTests(extra_args: list[str] | None = None) -> int:
    """Run every discovered test project and return an aggregate exit code.

    Returns non-zero if any individual test run returned non-zero.
    """
    test_projects = FindTestProjects()
    if not test_projects:
        print("No test projects found.")
        return 0

    aggregate_code = 0
    for proj in test_projects:
        print(f"\n{'=' * 80}")
        print(f"Running tests: {proj.projectName}")
        print(f"{'=' * 80}")
        code = RunTestProject(proj, extra_args)
        if code != 0:
            print(f"{proj.projectName} exited with code {code}")
            aggregate_code = code
        else:
            print(f"{proj.projectName} passed.")

    print(f"\n{'=' * 80}")
    if aggregate_code == 0:
        print("All test projects passed.")
    else:
        print(f"One or more test projects failed (aggregate exit code: {aggregate_code}).")
    print(f"{'=' * 80}\n")
    return aggregate_code

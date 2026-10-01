# Host unit tests live in winject-l3 (CMake + Google Test), not a PIO env.
import os

Import("env")  # pylint: disable=undefined-variable

project_dir = env["PROJECT_DIR"]  # pylint: disable=undefined-variable
l3_root = os.environ.get(
    "WINJECT_L3_ROOT", os.path.normpath(os.path.join(project_dir, "..", "winject-l3"))
)
test_dir = os.path.join(l3_root, "src", "test")
test_build = os.environ.get(
    "WINJECT_L3_TEST_BUILD", os.path.join(l3_root, "build_test_arm")
)

env.AddCustomTarget(  # pylint: disable=undefined-variable
    name="test",
    dependencies=None,
    actions=[
        f"cmake -S {test_dir} -B {test_build} -DCMAKE_BUILD_TYPE=Release",
        f"cmake --build {test_build}",
        f"ctest --test-dir {test_build} --output-on-failure",
    ],
    title="test",
    description="Build and run host Google tests (winject-l3)",
    always_build=True,
)

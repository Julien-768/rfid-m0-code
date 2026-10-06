from typing import Any

from SCons.Script import Import

env: Any
Import("env")

print("Project dir:", env["PROJECT_DIR"])
env.AddPreAction(
    "buildprog",
    env.VerboseAction("pio run -t compiledb", "Generating compile_commands.json"),
)
print(">>> gen_compdb.py executed")

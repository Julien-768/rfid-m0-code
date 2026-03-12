import os
from pathlib import Path
Import("env")

def bin_to_uf2(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    bin_path = build_dir / "firmware.bin"
    uf2_path = build_dir / "moonraket.uf2"

    # Ajoute des guillemets autour des chemins
    cmd = f'node tools/bin2uf2.js "{bin_path}" "{uf2_path}"'
    print("Converting to UF2:", cmd)
    os.system(cmd)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", bin_to_uf2)



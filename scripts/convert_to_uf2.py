"""
PlatformIO post-build script.

Converts the generated firmware.bin image into a UF2 image compatible
with the Adafruit Feather M0 bootloader and stores it in the firmware/
directory at the project root.
"""

from pathlib import Path

Import("env")

# Application start address after the UF2 bootloader on SAMD21.
APP_START_ADDRESS = 0x00002000

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30

PAYLOAD_SIZE = 256
BLOCK_SIZE = 512


def write_u32_le(block, offset, value):
    block[offset:offset + 4] = value.to_bytes(4, "little")


def convert_bin_to_uf2(bin_path, uf2_path):
    """
    Convert a raw firmware binary into a UF2 image.

    Args:
        bin_path: Input firmware.bin file.
        uf2_path: Output UF2 file.
    """
    data = bin_path.read_bytes()

    if not data:
        raise RuntimeError(f"{bin_path} is empty")

    num_blocks = (len(data) + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE
    blocks = []

    for block_no, pos in enumerate(range(0, len(data), PAYLOAD_SIZE)):
        block = bytearray(BLOCK_SIZE)

        write_u32_le(block, 0, UF2_MAGIC_START0)
        write_u32_le(block, 4, UF2_MAGIC_START1)
        write_u32_le(block, 8, 0)
        write_u32_le(block, 12, APP_START_ADDRESS + pos)
        write_u32_le(block, 16, PAYLOAD_SIZE)
        write_u32_le(block, 20, block_no)
        write_u32_le(block, 24, num_blocks)
        write_u32_le(block, 28, 0)

        payload = data[pos:pos + PAYLOAD_SIZE]
        block[32:32 + len(payload)] = payload

        write_u32_le(block, BLOCK_SIZE - 4, UF2_MAGIC_END)
        blocks.append(block)

    uf2_path.write_bytes(b"".join(blocks))
    print(f"Wrote {num_blocks} blocks to {uf2_path}")


def bin_to_uf2(source, target, env):
    """
    PlatformIO post-build callback.

    Generates firmware/rfid_door.uf2 from the compiled firmware.bin.
    """
    project_dir = Path(env.subst("$PROJECT_DIR"))

    firmware_dir = project_dir / "firmware"
    firmware_dir.mkdir(parents=True, exist_ok=True)

    build_dir = Path(env.subst("$BUILD_DIR"))
    bin_path = build_dir / "firmware.bin"
    uf2_path = firmware_dir / "rfid_door.uf2"

    print(f"Converting to UF2: {bin_path} -> {uf2_path}")
    convert_bin_to_uf2(bin_path, uf2_path)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", bin_to_uf2)
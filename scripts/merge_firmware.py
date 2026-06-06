#
# PlatformIO post-build hook: produce a single, flash-at-0x0 image.
#
# After a normal build, PlatformIO emits separate images (bootloader, partition
# table, OTA data, application) that must each be written to a specific offset.
# That is awkward for end users. This hook merges them into one file:
#
#   .pio/build/<env>/<progname>-merged.bin
#
# which can be flashed in a single step:
#
#   esptool --chip esp32c3 write-flash 0x0 shortbread-merged.bin
#
# or dropped straight into a browser flasher (ESP Web Tools / esptool-js) at
# offset 0x0. Offsets and flash settings are taken from the active build, so
# this stays correct if the partition layout ever changes.
#
Import("env")  # noqa: F821 — provided by PlatformIO's SCons environment

APP_BIN = "$BUILD_DIR/${PROGNAME}.bin"
# Friendly, distributable name — this is what the README and releases reference.
MERGED_BIN = "$BUILD_DIR/shortbread-merged.bin"
board = env.BoardConfig()


def _flash_freq():
    # PlatformIO exposes the frequency as raw Hz (e.g. "80000000L"); esptool's
    # merge-bin wants the short form ("80m"). Normalise, falling back to 80m.
    raw = str(board.get("build.f_flash", "80m")).lower().rstrip("l")
    if raw.endswith("m"):
        return raw
    try:
        return "{}m".format(int(raw) // 1000000)
    except ValueError:
        return "80m"


def merge_bin(source, target, env):
    # (offset, image) pairs for bootloader / partitions / boot_app0, then the app.
    flash_images = [
        *env.Flatten(env.get("FLASH_EXTRA_IMAGES", [])),
        "$ESP32_APP_OFFSET",
        APP_BIN,
    ]
    merge_cmd = " ".join(
        [
            '"$PYTHONEXE"',
            '"$OBJCOPY"',
            "--chip",
            board.get("build.mcu", "esp32c3"),
            "merge-bin",
            "-o",
            MERGED_BIN,
            "--flash-mode",
            board.get("build.flash_mode", "dio"),
            "--flash-freq",
            _flash_freq(),
            "--flash-size",
            board.get("upload.flash_size", "16MB"),
            *flash_images,
        ]
    )
    env.Execute(merge_cmd)
    print("Merged firmware image written to " + env.subst(MERGED_BIN))


# Run after the application .bin is built.
env.AddPostAction(APP_BIN, merge_bin)

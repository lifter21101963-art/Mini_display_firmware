Import("env")

import os


def merge_bin(target, source, env):
    build_dir = env.subst("$BUILD_DIR")
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")

    flash_size = env.GetProjectOption("board_upload.flash_size", "4MB")
    flash_mode = env.GetProjectOption("board_build.flash_mode", "dio")

    output = os.path.join(build_dir, "merged-firmware.bin")
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")

    print(f"Tworzenie jednego pliku bin: {output}")

    env.Execute(
        f'"$PYTHONEXE" "{esptool}" --chip esp32s3 merge_bin '
        f'-o "{output}" --flash_mode {flash_mode} --flash_size {flash_size} '
        f'0x0 "{bootloader}" 0x8000 "{partitions}" 0x10000 "{firmware}"'
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)

"""Bazel rules for building Particle P2 firmware with two-pass linking.

This module implements two-pass linking for accurate OTA-compatible module
boundaries, matching Particle's Make-based build system behavior.
"""

load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "ACTION_NAMES")
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")
load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("@pigweed//pw_build:binary_tools.bzl", "pw_elf_to_bin")
load("@pigweed//pw_toolchain/action:action_names.bzl", "PW_ACTION_NAMES")

# Platform: P2 (RTL8721DM / Cortex-M33)
PARTICLE_P2_PLATFORM_ID = 32
PARTICLE_P2_PLATFORM_NAME = "p2"
PARTICLE_P2_PLATFORM_GEN = 3

# Device OS version
DEVICE_OS_VERSION = 6304  # 6.3.4

# Particle-specific compiler flags
PARTICLE_COPTS = [
    "-flto",
    "-ffat-lto-objects",
    # Relax warnings for Device OS code
    "-Wno-redundant-decls",
    "-Wno-switch",
    "-Wno-unused-parameter",
    "-Wno-cast-qual",
    "-Wno-missing-field-initializers",
]

# Base linker flags (without memory-specific linker script)
PARTICLE_BASE_LINKOPTS = [
    # Cortex-M33 architecture flags (must match compilation)
    "-mcpu=cortex-m33",
    "-mthumb",
    "-mfloat-abi=hard",
    "-mfpu=fpv5-sp-d16",
    "--specs=nano.specs",
    "--specs=nosys.specs",
    # Linker scripts and library paths
    "-Tthird_party/particle/device-os/modules/tron/user-part/linker.ld",
    "-Lthird_party/particle/rules",  # For memory_platform_user.ld defaults
    "-Lthird_party/particle/device-os/modules/tron/user-part",
    "-Lthird_party/particle/device-os/modules/tron/system-part1",
    "-Lthird_party/particle/device-os/modules/shared/rtl872x",
    "-Lthird_party/particle/device-os/build/arm/linker",
    "-Lthird_party/particle/device-os/build/arm/linker/rtl872x",
    "-Wl,--defsym,__STACKSIZE__=8192",
    "-Wl,--defsym,__STACK_SIZE=8192",
    # Force linker to include user entry points
    "-Wl,--undefined=setup",
    "-Wl,--undefined=loop",
    "-Wl,--undefined=module_user_init_hook",
    "-Wl,--undefined=_post_loop",
    "-Wl,--gc-sections",
    "-Wl,--build-id",  # Generate GNU build ID
    "-Wl,--no-warn-rwx-segment",  # Suppress RWX segment warnings
    "-nostartfiles",
    "-fno-lto",  # Disable LTO at link time (as per Particle build)
    "-lnosys",
    "-lc",
    "-lm",
    "-lstdc++",
]

def _particle_two_pass_binary_impl(ctx):
    """Implementation of two-pass linking for Particle firmware.

    Pass 1: Link with conservative defaults to create intermediate ELF
    Pass 2: Extract sizes, generate precise linker script, re-link final ELF
    """
    cc_toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )

    # Get toolchain paths
    linker_path = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.cpp_link_executable,
    )
    objdump_path = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = PW_ACTION_NAMES.objdump_disassemble,
    )

    # Collect all object files and libraries from deps
    objects = []
    linker_inputs = []
    for dep in ctx.attr.deps:
        if CcInfo in dep:
            cc_info = dep[CcInfo]
            for li in cc_info.linking_context.linker_inputs.to_list():
                for lib in li.libraries:
                    if lib.static_library:
                        linker_inputs.append(lib.static_library)
                    if lib.pic_static_library:
                        linker_inputs.append(lib.pic_static_library)
                    if lib.objects:
                        objects.extend(lib.objects)
                    if lib.pic_objects:
                        objects.extend(lib.pic_objects)

    # Collect linker scripts
    linker_script_files = []
    for f in ctx.files._linker_scripts:
        linker_script_files.append(f)

    # Output files
    intermediate_elf = ctx.actions.declare_file(ctx.attr.name + "_intermediate.elf")
    sizes_json = ctx.actions.declare_file(ctx.attr.name + "_sizes.json")
    # Name must match what linker.ld includes: "memory_platform_user.ld"
    # Put in subdirectory so we can add it first in -L search path
    precise_ld = ctx.actions.declare_file(ctx.attr.name + "_generated/memory_platform_user.ld")
    final_elf = ctx.actions.declare_file(ctx.attr.name)

    # Build linker flags for first pass (with defaults from memory_platform_user.ld)
    base_linkopts = PARTICLE_BASE_LINKOPTS
    user_linkopts = ctx.attr.linkopts

    # Pass 2 flags: put generated memory_platform_user.ld directory FIRST
    # so it overrides the defaults when linker.ld does INCLUDE memory_platform_user.ld
    pass2_linkopts = ["-L" + precise_ld.dirname] + PARTICLE_BASE_LINKOPTS

    # Create the two-pass linking script
    # We use run_shell because we need to execute two linking steps with
    # dynamic generation of the linker script between them
    script = """
set -e

# === PASS 1: Link with defaults ===
echo "Pass 1: Linking with default memory values..."
{linker} {linker_flags} {objects} {libraries} -o {intermediate_elf}

# === Extract sizes from intermediate ELF ===
echo "Extracting section sizes..."
python3 {extract_script} \
    --objdump {objdump} \
    --elf {intermediate_elf} \
    --output-json {sizes_json} \
    --output-ld {precise_ld}

# === PASS 2: Re-link with precise values ===
echo "Pass 2: Re-linking with precise memory values..."
{linker} {linker_flags_pass2} {objects} {libraries} -o {final_elf}

echo "Two-pass linking complete."
""".format(
        linker = linker_path,
        extract_script = ctx.file._extract_sizes.path,
        objdump = objdump_path,
        linker_flags = " ".join(base_linkopts + user_linkopts),
        linker_flags_pass2 = " ".join(pass2_linkopts + user_linkopts),
        objects = " ".join([o.path for o in objects]),
        libraries = " ".join(["-l:" + l.basename + " -L" + l.dirname for l in linker_inputs]),
        intermediate_elf = intermediate_elf.path,
        sizes_json = sizes_json.path,
        precise_ld = precise_ld.path,
        final_elf = final_elf.path,
    )

    ctx.actions.run_shell(
        outputs = [intermediate_elf, sizes_json, precise_ld, final_elf],
        inputs = depset(
            direct = objects + linker_inputs + linker_script_files + [ctx.file._extract_sizes],
            transitive = [cc_toolchain.all_files],
        ),
        command = script,
        mnemonic = "ParticleTwoPassLink",
        progress_message = "Two-pass linking %{label}",
        use_default_shell_env = True,
    )

    return [
        DefaultInfo(
            files = depset([final_elf, sizes_json]),
            executable = final_elf,
        ),
    ]

_particle_two_pass_binary = rule(
    implementation = _particle_two_pass_binary_impl,
    attrs = {
        "deps": attr.label_list(providers = [CcInfo]),
        "linkopts": attr.string_list(default = []),
        "_linker_scripts": attr.label(
            default = "//particle:linker_scripts",
        ),
        "_extract_sizes": attr.label(
            default = "//tools:extract_elf_sizes.py",
            allow_single_file = True,
        ),
        "_cc_toolchain": attr.label(
            default = "@bazel_tools//tools/cpp:current_cc_toolchain",
        ),
    },
    executable = True,
    toolchains = use_cpp_toolchain(),
    fragments = ["cpp"],
)

def particle_cc_binary(
        name,
        srcs = [],
        deps = [],
        copts = [],
        defines = [],
        linkopts = [],
        two_pass = True,
        **kwargs):
    """Creates a Particle P2 firmware binary.

    This macro creates a firmware binary with proper two-pass linking for
    OTA-compatible module boundaries. It wraps cc_binary/cc_library with
    all the Particle-specific compiler flags, linker scripts, and
    dependencies needed to build a user firmware module.

    Args:
        name: Target name. Should end with .elf for clarity.
        srcs: Source files for the firmware.
        deps: Dependencies (user libraries).
        copts: Additional compiler flags.
        defines: Additional preprocessor defines.
        linkopts: Additional linker flags.
        two_pass: If True (default), use two-pass linking for precise memory.
                  If False, use single-pass with static defaults.
        **kwargs: Additional arguments passed to the underlying rules.
    """
    # Create a library with the firmware sources
    lib_name = name + "_lib"
    native.cc_library(
        name = lib_name,
        srcs = srcs,
        deps = deps + [
            "//particle:device_os_user_part",
            "@pigweed//pw_assert_basic",
        ],
        copts = PARTICLE_COPTS + copts,
        defines = defines,
        alwayslink = True,
        target_compatible_with = ["@pigweed//pw_build/constraints/arm:cortex-m33"],
        **{k: v for k, v in kwargs.items() if k not in ["visibility"]}
    )

    if two_pass:
        # Use two-pass linking for precise memory boundaries
        _particle_two_pass_binary(
            name = name,
            deps = [":" + lib_name],
            linkopts = linkopts,
            **{k: v for k, v in kwargs.items() if k in ["visibility", "tags", "testonly"]}
        )
    else:
        # Fallback: single-pass linking with static defaults
        particle_linkopts = PARTICLE_BASE_LINKOPTS + [
            "-Lthird_party/particle/rules",  # For memory_platform_user.ld
        ]
        cc_binary(
            name = name,
            deps = [":" + lib_name],
            additional_linker_inputs = [
                "//particle:linker_scripts",
            ],
            linkopts = particle_linkopts + linkopts,
            target_compatible_with = ["@pigweed//pw_build/constraints/arm:cortex-m33"],
            **{k: v for k, v in kwargs.items() if k in ["visibility", "tags", "testonly"]}
        )

def particle_firmware_binary(
        name,
        elf,
        **kwargs):
    """Creates a flashable .bin from an ELF with SHA256/CRC32 patched.

    Args:
        name: Target name for the .bin file.
        elf: Label of the ELF file to convert.
        **kwargs: Additional arguments passed to genrule.
    """
    # Use Pigweed's pw_elf_to_bin for proper toolchain integration
    bin_name = name + "_raw"
    pw_elf_to_bin(
        name = bin_name,
        elf_input = elf,
        bin_out = bin_name + ".bin",
    )

    # Patch SHA256 and CRC32 into the binary
    native.genrule(
        name = name,
        srcs = [":" + bin_name + ".bin"],
        outs = [name + ".bin"],
        cmd = """
            cp $< $@
            chmod +w $@
            python3 $(location @particle_bazel//tools:particle_crc) $@
        """,
        tools = ["@particle_bazel//tools:particle_crc"],
        target_compatible_with = ["@pigweed//pw_build/constraints/arm:cortex-m33"],
        **kwargs
    )

def particle_flash_binary(
        name,
        firmware,
        **kwargs):
    """Creates a target to flash firmware to a Particle device.

    Args:
        name: Target name.
        firmware: Label of the firmware binary (.bin or .elf) to flash.
        **kwargs: Additional arguments passed to sh_binary.
    """
    native.sh_binary(
        name = name,
        srcs = ["//particle/rules:flash.sh"],
        data = [firmware],
        args = ["$(location " + firmware + ")"],
        tags = ["local"],  # Bypass sandbox to access particle credentials
        **kwargs
    )

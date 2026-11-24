#!/usr/bin/env python3
"""FSR 2.2.1's Vulkan shader permutations, built without AMD's FidelityFX_SC.exe (a Windows tool).

The FSR 2 Vulkan backend (src/ffx-fsr2-api/vk/shaders/ffx_fsr2_shaders_vk.cpp) includes one
<pass>_permutations.h per pass: a permutation key (a bitfield union), an indirection table from the
key's index to a blob, and per blob the SPIR-V with the names and bindings of the images and uniform
buffers it uses (the backend binds resources by those names). This script writes those headers with
glslc and its own SPIR-V reflection.

Only the permutations the engine creates are compiled (switch/README.md, FSR 2): render-resolution
motion vectors without jitter, inverted (reversed-Z) depth. HDR colour input (FSR 2 before the
post-processing) or not (after it, the Switch 2's order), the Lanczos LUT and the sharpening bits are
compiled both ways; FP16 is not (the 16-bit key bit maps to the 32-bit shaders:
NVIDIA's accumulate pass drops it anyway, and fewer shader paths go through ouncevk's compiler). Every
other key maps to the compiled shader with the same HDR, LUT and sharpening bits, so the backend must be
created with exactly the flags above (fsr2_backend.cpp does).

usage: fsr2_vk_shaders.py --glslc <glslc> --src <ffx-fsr2-api/shaders> --out <dir>
"""
import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

PASSES = [
    "ffx_fsr2_tcr_autogen_pass",
    "ffx_fsr2_autogen_reactive_pass",
    "ffx_fsr2_accumulate_pass",
    "ffx_fsr2_compute_luminance_pyramid_pass",
    "ffx_fsr2_depth_clip_pass",
    "ffx_fsr2_lock_pass",
    "ffx_fsr2_reconstruct_previous_depth_pass",
    "ffx_fsr2_rcas_pass",
]

# the key's bits, in the order of ffx_fsr2_shaders_vk.cpp's POPULATE_PERMUTATION_KEY (bit 0 first)
KEY_BITS = [
    "FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE",
    "FFX_FSR2_OPTION_HDR_COLOR_INPUT",
    "FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS",
    "FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS",
    "FFX_FSR2_OPTION_INVERTED_DEPTH",
    "FFX_FSR2_OPTION_APPLY_SHARPENING",
    "FFX_HALF",
]
FIXED = {
    "FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS": 1,
    "FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS": 0,
    "FFX_FSR2_OPTION_INVERTED_DEPTH": 1,
    "FFX_HALF": 0,
}
# FSR 2.2.1's src/ffx-fsr2-api/CMakeLists.txt (FFX_SC_BASE_ARGS) and vk/CMakeLists.txt (FFX_SC_VK_BASE_ARGS)
BASE_DEFINES = {
    "FFX_GLSL": 1,
    "FFX_GPU": 1,
    "FFX_FSR2_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF": 0,
    "FFX_FSR2_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF": 0,
    "FFX_FSR2_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF": 1,
    "FFX_FSR2_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF": 0,
    "FFX_FSR2_OPTION_UPSAMPLE_USE_LANCZOS_TYPE": 2,
}


def compile_pass(glslc, src, name, defines):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "pass.spv")
        # -g keeps OpName (the backend binds resources by name, and -O alone strips names); strip_debug drops the rest of it
        cmd = [glslc, "-fshader-stage=compute", "--target-env=vulkan1.1", "-O", "-g", "-I", src]
        cmd += ["-D%s=%d" % (k, v) for k, v in sorted(defines.items())]
        cmd += [os.path.join(src, name + ".glsl"), "-o", out]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            sys.stderr.write(result.stdout + result.stderr)
            raise SystemExit("glslc failed on %s" % name)
        with open(out, "rb") as f:
            return strip_debug(f.read())


def strip_debug(spirv):
    """The module without its source text, line and member-name information (OpName stays: the reflection needs it)."""
    words = struct.unpack("<%dI" % (len(spirv) // 4), spirv)
    kept = list(words[:5])
    i = 5
    while i < len(words):
        count = words[i] >> 16
        if (words[i] & 0xFFFF) not in (2, 3, 4, 6, 7, 8, 317, 330):  # OpSource*, OpMemberName, OpString, OpLine, OpNoLine, OpModuleProcessed
            kept += words[i:i + count]
        i += count
    return struct.pack("<%dI" % len(kept), *kept)


def reflect(spirv):
    """(sampled images, storage images, uniform buffers) used by the module: lists of (name, binding), by binding."""
    words = struct.unpack("<%dI" % (len(spirv) // 4), spirv)
    if words[0] != 0x07230203:
        raise SystemExit("not SPIR-V")
    names, bindings, sets, types, pointers, variables = {}, {}, {}, {}, {}, []
    i = 5
    while i < len(words):
        op = words[i] & 0xFFFF
        count = words[i] >> 16
        args = words[i + 1:i + count]
        if op == 5:  # OpName
            raw = struct.pack("<%dI" % (count - 2), *args[1:])
            names[args[0]] = raw.split(b"\0", 1)[0].decode()
        elif op == 71 and args[1] == 33:  # OpDecorate Binding
            bindings[args[0]] = args[2]
        elif op == 71 and args[1] == 34:  # OpDecorate DescriptorSet
            sets[args[0]] = args[2]
        elif op == 25:  # OpTypeImage: sampled operand 1 = sampled image, 2 = storage image
            types[args[0]] = ("image", args[6])
        elif op == 28:  # OpTypeArray
            types[args[0]] = ("array", args[1])
        elif op == 30:  # OpTypeStruct
            types[args[0]] = ("struct", 0)
        elif op == 32:  # OpTypePointer
            pointers[args[0]] = args[2]
        elif op == 59:  # OpVariable
            variables.append((args[1], args[0], args[2]))
        i += count
    srv, uav, cb = [], [], []
    for var, ptr_type, storage in variables:
        if var not in bindings or sets.get(var, 0) != 1:
            continue  # set 0 holds the backend's immutable samplers
        kind, sampled = types.get(pointers.get(ptr_type), (None, 0))
        if kind == "array":
            kind, sampled = types.get(sampled, (None, 0))
        name = names.get(var, "")
        if kind == "image" and storage == 0:
            (srv if sampled == 1 else uav).append((name, bindings[var]))
        elif kind == "struct" and storage == 2:
            if not name:
                name = names.get(pointers.get(ptr_type), "")
                name = name[:-2] if name.endswith("_t") else name
            cb.append((name, bindings[var]))
    return [sorted(x, key=lambda e: e[1]) for x in (srv, uav, cb)]


def write_header(path, name, blobs, table):
    lines = ["// Generated by tools/fsr2_vk_shaders.py from FSR 2.2.1 (MIT, Advanced Micro Devices). Do not edit.", "#pragma once", "#include <stdint.h>", ""]
    lines.append("typedef union %s_PermutationKey {" % name)
    lines.append("    struct {")
    lines += ["        uint32_t %s : 1;" % bit for bit in KEY_BITS]
    lines.append("    };")
    lines.append("    uint32_t index;")
    lines.append("} %s_PermutationKey;" % name)
    lines.append("")
    lines.append("typedef struct %s_PermutationInfo {" % name)
    lines.append("    const unsigned char* blobData;")
    lines.append("    uint32_t blobSize;")
    for kind in ("StorageImage", "SampledImage", "UniformBuffer"):
        lines.append("    uint32_t num%sResources;" % kind)
        lines.append("    const char** %sResourceNames;" % (kind[0].lower() + kind[1:]))
        lines.append("    const uint32_t* %sResourceBindings;" % (kind[0].lower() + kind[1:]))
    lines.append("} %s_PermutationInfo;" % name)
    lines.append("")
    infos = []
    for n, (spirv, (srv, uav, cb)) in enumerate(blobs):
        words = struct.unpack("<%dI" % (len(spirv) // 4), spirv)
        tag = "g_%s_%d" % (name, n)
        lines.append("static const uint32_t %s_code[%d] = {" % (tag, len(words)))
        for at in range(0, len(words), 12):
            lines.append("    " + ", ".join("0x%08x" % w for w in words[at:at + 12]) + ",")
        lines.append("};")
        fields = []
        for kind, items in (("uav", uav), ("srv", srv), ("cb", cb)):
            if items:
                lines.append("static const char* %s_%s_names[] = {%s};" % (tag, kind, ", ".join('"%s"' % e[0] for e in items)))
                lines.append("static const uint32_t %s_%s_bindings[] = {%s};" % (tag, kind, ", ".join(str(e[1]) for e in items)))
                fields.append("%d, %s_%s_names, %s_%s_bindings" % (len(items), tag, kind, tag, kind))
            else:
                fields.append("0, nullptr, nullptr")
        infos.append("    {reinterpret_cast<const unsigned char*>(%s_code), %d, %s}," % (tag, len(spirv), ", ".join(fields)))
    lines.append("")
    lines.append("static const int32_t g_%s_IndirectionTable[%d] = {%s};" % (name, len(table), ", ".join(str(t) for t in table)))
    lines.append("static const %s_PermutationInfo g_%s_PermutationInfo[] = {" % (name, name))
    lines += infos
    lines.append("};")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--glslc", required=True)
    parser.add_argument("--src", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    hdr_bit = KEY_BITS.index("FFX_FSR2_OPTION_HDR_COLOR_INPUT")
    lut_bit = KEY_BITS.index("FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE")
    sharpen_bit = KEY_BITS.index("FFX_FSR2_OPTION_APPLY_SHARPENING")
    for name in PASSES:
        blobs, by_hash, chosen = [], {}, {}
        for hdr in (0, 1):
            for lut in (0, 1):
                for sharpen in (0, 1):
                    defines = dict(BASE_DEFINES)
                    defines.update(FIXED)
                    defines["FFX_FSR2_OPTION_HDR_COLOR_INPUT"] = hdr
                    defines["FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE"] = lut
                    defines["FFX_FSR2_OPTION_APPLY_SHARPENING"] = sharpen
                    spirv = compile_pass(args.glslc, args.src, name, defines)
                    digest = hashlib.sha256(spirv).hexdigest()
                    if digest not in by_hash:
                        by_hash[digest] = len(blobs)
                        blobs.append((spirv, reflect(spirv)))
                    chosen[(hdr, lut, sharpen)] = by_hash[digest]
        table = [chosen[((key >> hdr_bit) & 1, (key >> lut_bit) & 1, (key >> sharpen_bit) & 1)] for key in range(1 << len(KEY_BITS))]
        write_header(os.path.join(args.out, name + "_permutations.h"), name, blobs, table)
        print("fsr2: %s, %d shader(s)" % (name, len(blobs)))


if __name__ == "__main__":
    main()

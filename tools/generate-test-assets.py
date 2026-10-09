#!/usr/bin/env python3
"""Generates the models, textures and environment maps that Lodestone's tests use, and the fuzz corpus seeds made
from them. Everything is made from code, so the assets have no license of their own and can be rebuilt exactly.

    python tools/generate-test-assets.py

Writes Tests/Fixtures/Models, Tests/Fixtures/Environments, and Tests/Fuzz/Corpus/{Model,Image,Hdr}. The binary files
are stored with Git LFS. Only the standard library is used.
"""

import base64
import json
import math
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
MODELS = ROOT / "Tests" / "Fixtures" / "Models"
ENVIRONMENTS = ROOT / "Tests" / "Fixtures" / "Environments"
CORPUS = ROOT / "Tests" / "Fuzz" / "Corpus"


# --- Images ---------------------------------------------------------------------------------------------------------

def encode_png(width, height, pixels):
    """An RGBA8 PNG; pixels is a list of (r, g, b, a) rows, top first."""
    raw = bytearray()
    for row in pixels:
        raw.append(0)
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
            chunk(b"IEND", b""))


def encode_hdr(width, height, pixels):
    """A Radiance HDR file, uncompressed RGBE; pixels is a list of (r, g, b) float rows, top first."""
    data = bytearray(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
    data += f"-Y {height} +X {width}\n".encode()
    for row in pixels:
        for r, g, b in row:
            brightest = max(r, g, b)
            if brightest < 1e-32:
                data += bytes((0, 0, 0, 0))
                continue
            mantissa, exponent = math.frexp(brightest)
            scale = mantissa * 256.0 / brightest
            data += bytes((int(r * scale), int(g * scale), int(b * scale), exponent + 128))
    return bytes(data)


# A minimal baseline JPEG encoder: one grayscale component, the standard quantization and Huffman tables
ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7,
          14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46,
          53, 60, 61, 54, 47, 55, 62, 63]
# In natural order, row by row
LUMINANCE_QUANTIZATION = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69,
                          56, 14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81,
                          104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99]
DC_LENGTHS = [0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0]
DC_VALUES = list(range(12))
AC_LENGTHS = [0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D]
AC_VALUES = [
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14,
    0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09,
    0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A,
    0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65,
    0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
    0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9,
    0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA,
    0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
    0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA]


def huffman_codes(lengths, values):
    codes = {}
    code = 0
    index = 0
    for length in range(1, 17):
        for _ in range(lengths[length - 1]):
            codes[values[index]] = (code, length)
            code += 1
            index += 1
        code <<= 1
    return codes


def encode_jpeg(width, height, gray):
    """A grayscale baseline JPEG; gray is a list of rows of 0-255 values, top first. Width and height are multiples of
    8."""
    dc_codes = huffman_codes(DC_LENGTHS, DC_VALUES)
    ac_codes = huffman_codes(AC_LENGTHS, AC_VALUES)
    bits = []

    def write(code, length):
        for shift in range(length - 1, -1, -1):
            bits.append((code >> shift) & 1)

    def magnitude(value):
        size = abs(value).bit_length()
        return size, (value if value >= 0 else value + (1 << size) - 1)

    previous_dc = 0
    for block_y in range(0, height, 8):
        for block_x in range(0, width, 8):
            samples = [[gray[block_y + y][block_x + x] - 128 for x in range(8)] for y in range(8)]
            coefficients = []
            for v in range(8):
                for u in range(8):
                    total = 0.0
                    for y in range(8):
                        for x in range(8):
                            total += (samples[y][x] * math.cos((2 * x + 1) * u * math.pi / 16) *
                                      math.cos((2 * y + 1) * v * math.pi / 16))
                    cu = 1 / math.sqrt(2) if u == 0 else 1.0
                    cv = 1 / math.sqrt(2) if v == 0 else 1.0
                    coefficients.append(0.25 * cu * cv * total)
            quantized = [int(round(coefficients[ZIGZAG[k]] / LUMINANCE_QUANTIZATION[ZIGZAG[k]])) for k in range(64)]
            difference = quantized[0] - previous_dc
            previous_dc = quantized[0]
            size, value = magnitude(difference)
            write(*dc_codes[size])
            write(value, size)
            run = 0
            for k in range(1, 64):
                if quantized[k] == 0:
                    run += 1
                    continue
                while run > 15:
                    write(*ac_codes[0xF0])
                    run -= 16
                size, value = magnitude(quantized[k])
                write(*ac_codes[(run << 4) | size])
                write(value, size)
                run = 0
            if run > 0:
                write(*ac_codes[0x00])
    while len(bits) % 8 != 0:
        bits.append(1)
    scan = bytearray()
    for index in range(0, len(bits), 8):
        byte = int("".join(map(str, bits[index:index + 8])), 2)
        scan.append(byte)
        if byte == 0xFF:
            scan.append(0)

    def segment(marker, data):
        return bytes((0xFF, marker)) + struct.pack(">H", len(data) + 2) + data

    # The table goes in zigzag order, like the coefficients
    quantization = bytes([0]) + bytes(LUMINANCE_QUANTIZATION[ZIGZAG[k]] for k in range(64))
    frame = struct.pack(">BHHB", 8, height, width, 1) + bytes((1, 0x11, 0))
    huffman = (bytes([0x00]) + bytes(DC_LENGTHS) + bytes(DC_VALUES) + bytes([0x10]) + bytes(AC_LENGTHS) +
               bytes(AC_VALUES))
    start_of_scan = bytes((1, 1, 0x00, 0, 63, 0))
    return (b"\xFF\xD8" + segment(0xDB, quantization) + segment(0xC0, frame) + segment(0xC4, huffman) +
            segment(0xDA, start_of_scan) + bytes(scan) + b"\xFF\xD9")


def checker(size, cell, first, second):
    return [[first if ((x // cell) + (y // cell)) % 2 == 0 else second for x in range(size)] for y in range(size)]


def normal_map(size):
    """Round bumps, as tangent-space normals."""
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            u = (x + 0.5) / size * 4 * math.pi
            v = (y + 0.5) / size * 4 * math.pi
            dx = 0.5 * math.cos(u) * math.sin(v)
            dy = 0.5 * math.sin(u) * math.cos(v)
            length = math.sqrt(dx * dx + dy * dy + 1)
            normal = (-dx / length, -dy / length, 1 / length)
            row.append(tuple(int(round((component * 0.5 + 0.5) * 255)) for component in normal) + (255,))
        rows.append(row)
    return rows


def metallic_roughness_map(size):
    """Roughness rising left to right in green; metal in blue in the top half."""
    return [[(0, int(round(255 * (0.1 + 0.8 * x / (size - 1)))), 255 if y < size // 2 else 0, 255)
             for x in range(size)] for y in range(size)]


def leaf_mask(size):
    """A disc, opaque inside and transparent outside, with a soft edge."""
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            distance = math.hypot(x + 0.5 - size / 2, y + 0.5 - size / 2) / (size / 2)
            alpha = max(0.0, min(1.0, (0.8 - distance) * 10 + 0.5))
            row.append((60, 160, 70, int(round(alpha * 255))))
        rows.append(row)
    return rows


def sky(width, height, sun_elevation=35.0, sun_azimuth=45.0):
    """An equirectangular sky: a blue gradient, a bright horizon, dim ground, and a small bright sun."""
    sun = (math.cos(math.radians(sun_elevation)) * math.sin(math.radians(sun_azimuth)),
           math.sin(math.radians(sun_elevation)),
           math.cos(math.radians(sun_elevation)) * math.cos(math.radians(sun_azimuth)))
    rows = []
    for y in range(height):
        elevation = math.pi / 2 - (y + 0.5) / height * math.pi
        row = []
        for x in range(width):
            azimuth = (x + 0.5) / width * 2 * math.pi - math.pi
            direction = (math.cos(elevation) * math.sin(azimuth), math.sin(elevation),
                         math.cos(elevation) * math.cos(azimuth))
            if elevation >= 0:
                t = elevation / (math.pi / 2)
                color = tuple((1 - t) * horizon + t * zenith for horizon, zenith in zip((1.1, 1.05, 1.0),
                                                                                         (0.25, 0.45, 1.0)))
            else:
                color = (0.18, 0.16, 0.14)
            cosine = sum(a * b for a, b in zip(direction, sun))
            if cosine > math.cos(math.radians(3.0)):
                color = (60.0, 55.0, 48.0)
            row.append(color)
        rows.append(row)
    return rows


# --- glTF -----------------------------------------------------------------------------------------------------------

class Gltf:
    """Builds a glTF 2.0 document with one buffer."""

    def __init__(self):
        self.document = {"asset": {"version": "2.0", "generator": "Lodestone generate-test-assets.py"}}
        self.buffer = bytearray()

    def add(self, key, item):
        self.document.setdefault(key, []).append(item)
        return len(self.document[key]) - 1

    def view(self, data, target=None):
        while len(self.buffer) % 4 != 0:
            self.buffer.append(0)
        view = {"buffer": 0, "byteOffset": len(self.buffer), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.buffer += data
        return self.add("bufferViews", view)

    def accessor(self, values, kind, component=5126, minmax=False, target=34962):
        count = len(values)
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[kind]
        flat = [v for value in values for v in (value if width > 1 else (value,))]
        fmt = {5126: "f", 5123: "H", 5125: "I"}[component]
        accessor = {"bufferView": self.view(struct.pack(f"<{len(flat)}{fmt}", *flat), target), "componentType": component,
                    "count": count, "type": kind}
        if minmax:
            accessor["min"] = [min(value[i] for value in values) for i in range(width)]
            accessor["max"] = [max(value[i] for value in values) for i in range(width)]
        return self.add("accessors", accessor)

    def indices(self, values):
        return self.accessor(values, "SCALAR", 5123 if max(values) < 65536 else 5125, target=34963)

    def mesh_primitive(self, positions, normals=None, uvs=None, indices=None, material=None, mode=None):
        attributes = {"POSITION": self.accessor(positions, "VEC3", minmax=True)}
        if normals is not None:
            attributes["NORMAL"] = self.accessor(normals, "VEC3")
        if uvs is not None:
            attributes["TEXCOORD_0"] = self.accessor(uvs, "VEC2")
        primitive = {"attributes": attributes}
        if indices is not None:
            primitive["indices"] = self.indices(indices)
        if material is not None:
            primitive["material"] = material
        if mode is not None:
            primitive["mode"] = mode
        return primitive

    def embedded_image(self, data, mime):
        return self.add("images", {"bufferView": self.view(data), "mimeType": mime})

    def texture(self, image, sampler=None):
        texture = {"source": image}
        if sampler is not None:
            texture["sampler"] = sampler
        return self.add("textures", texture)

    def write_gltf(self, path, bin_name=None):
        """As .gltf, with the buffer in a file of its own or, without a name, embedded as a data URI."""
        document = dict(self.document)
        if bin_name is not None:
            document["buffers"] = [{"uri": bin_name, "byteLength": len(self.buffer)}]
            (path.parent / bin_name).write_bytes(bytes(self.buffer))
        else:
            encoded = base64.b64encode(bytes(self.buffer)).decode()
            document["buffers"] = [{"uri": "data:application/octet-stream;base64," + encoded,
                                    "byteLength": len(self.buffer)}]
        path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8", newline="\n")

    def write_glb(self, path):
        document = dict(self.document)
        document["buffers"] = [{"byteLength": len(self.buffer)}]
        text = json.dumps(document, separators=(",", ":")).encode()
        text += b" " * ((4 - len(text) % 4) % 4)
        binary = bytes(self.buffer) + b"\0" * ((4 - len(self.buffer) % 4) % 4)
        total = 12 + 8 + len(text) + 8 + len(binary)
        path.write_bytes(struct.pack("<4sII", b"glTF", 2, total) + struct.pack("<I4s", len(text), b"JSON") + text +
                         struct.pack("<I4s", len(binary), b"BIN\0") + binary)


def cube(size=1.0):
    half = size / 2
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    positions, normals, uvs, indices = [], [], [], []
    for normal, right, up in faces:
        base = len(positions)
        for u, v in ((0, 0), (1, 0), (1, 1), (0, 1)):
            positions.append(tuple(half * (n + (2 * u - 1) * r + (2 * v - 1) * w)
                                   for n, r, w in zip(normal, right, up)))
            normals.append(normal)
            uvs.append((u, 1 - v))
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, uvs, indices


def sphere(radius=0.5, segments=32, rings=16):
    positions, normals, uvs, indices = [], [], [], []
    for ring in range(rings + 1):
        theta = ring / rings * math.pi
        for segment in range(segments + 1):
            phi = segment / segments * 2 * math.pi
            normal = (math.sin(theta) * math.sin(phi), math.cos(theta), math.sin(theta) * math.cos(phi))
            positions.append(tuple(radius * n for n in normal))
            normals.append(normal)
            uvs.append((segment / segments, ring / rings))
    for ring in range(rings):
        for segment in range(segments):
            a = ring * (segments + 1) + segment
            b = a + segments + 1
            indices += [a, b, a + 1, a + 1, b, b + 1]
    return positions, normals, uvs, indices


def quad(size=1.0):
    half = size / 2
    positions = [(-half, -half, 0), (half, -half, 0), (half, half, 0), (-half, half, 0)]
    return positions, [(0, 0, 1)] * 4, [(0, 1), (1, 1), (1, 0), (0, 0)], [0, 1, 2, 0, 2, 3]


def textured_cube():
    """A cube with base color (JPEG), metallic-roughness and normal textures in files beside it, and no tangents."""
    gltf = Gltf()
    gltf.add("samplers", {"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497})
    images = [gltf.add("images", {"uri": "Textures/Checker.jpg"}),
              gltf.add("images", {"uri": "Textures/Metallic%20Roughness.png"}),
              gltf.add("images", {"uri": "Textures/Normal.png"})]
    textures = [gltf.texture(image, 0) for image in images]
    gltf.add("materials", {"name": "Checkered", "pbrMetallicRoughness": {
        "baseColorFactor": [1.0, 0.9, 0.8, 1.0], "baseColorTexture": {"index": textures[0]},
        "metallicRoughnessTexture": {"index": textures[1]}}, "normalTexture": {"index": textures[2], "scale": 1.0}})
    positions, normals, uvs, indices = cube()
    gltf.add("meshes", {"name": "Cube", "primitives": [gltf.mesh_primitive(positions, normals, uvs, indices, 0)]})
    gltf.add("nodes", {"name": "Cube", "mesh": 0})
    gltf.add("scenes", {"nodes": [0]})
    gltf.document["scene"] = 0
    textures_directory = MODELS / "Textures"
    textures_directory.mkdir(parents=True, exist_ok=True)
    gray = [[200 if ((x // 8) + (y // 8)) % 2 == 0 else 60 for x in range(64)] for y in range(64)]
    (textures_directory / "Checker.jpg").write_bytes(encode_jpeg(64, 64, gray))
    (textures_directory / "Metallic Roughness.png").write_bytes(encode_png(64, 64, metallic_roughness_map(64)))
    (textures_directory / "Normal.png").write_bytes(encode_png(64, 64, normal_map(64)))
    gltf.write_gltf(MODELS / "TexturedCube.gltf", "TexturedCube.bin")


def material_spheres():
    """A 5x5 grid of spheres, metalness rising along X and roughness along Y, in one binary file."""
    gltf = Gltf()
    positions, normals, uvs, indices = sphere()
    primitive = gltf.mesh_primitive(positions, normals, uvs, indices)
    nodes = []
    for row in range(5):
        for column in range(5):
            material = gltf.add("materials", {"name": f"Metal {column} Rough {row}", "pbrMetallicRoughness": {
                "baseColorFactor": [0.9, 0.6, 0.3, 1.0], "metallicFactor": column / 4,
                "roughnessFactor": max(0.05, row / 4)}})
            mesh = gltf.add("meshes", {"name": f"Sphere {row} {column}",
                                       "primitives": [dict(primitive, material=material)]})
            nodes.append(gltf.add("nodes", {"name": f"Sphere {row} {column}", "mesh": mesh,
                                            "translation": [(column - 2) * 1.2, (row - 2) * 1.2, 0.0]}))
    gltf.add("scenes", {"nodes": nodes})
    gltf.document["scene"] = 0
    gltf.write_glb(MODELS / "MaterialSpheres.glb")


def hierarchy():
    """Nodes with translations, a matrix, a rotation and a scale; a mesh of two primitives (a triangle list and a
    strip without indices or normals); a mesh without a material; and a node without a mesh. Embedded as a data URI."""
    gltf = Gltf()
    red = gltf.add("materials", {"name": "Red", "pbrMetallicRoughness": {"baseColorFactor": [1, 0, 0, 1],
                                                                          "metallicFactor": 0}})
    blue = gltf.add("materials", {"name": "Blue", "doubleSided": True, "emissiveFactor": [0, 0, 0.5],
                                  "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 2.0}},
                                  "pbrMetallicRoughness": {"baseColorFactor": [0, 0, 1, 1], "roughnessFactor": 0.25}})
    gltf.document["extensionsUsed"] = ["KHR_materials_emissive_strength"]
    positions, normals, uvs, indices = cube(0.5)
    strip = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0), (0, 2, 0)]
    two_parts = gltf.add("meshes", {"name": "Two Parts", "primitives": [
        gltf.mesh_primitive(positions, normals, uvs, indices, red),
        gltf.mesh_primitive(strip, material=blue, mode=5)]})
    plain = gltf.add("meshes", {"name": "Plain", "primitives": [gltf.mesh_primitive(*quad(), None)]})
    angle = math.radians(90)
    gltf.add("nodes", {"name": "Root", "translation": [1, 0, 0], "children": [1, 3]})
    gltf.add("nodes", {"name": "Turned", "matrix": [0, 0, -2, 0, 0, 2, 0, 0, 2, 0, 0, 0, 0, 3, 0, 1], "children": [2]})
    gltf.add("nodes", {"name": "Parts", "mesh": two_parts, "rotation": [0, math.sin(angle / 2), 0, math.cos(angle / 2)],
                       "scale": [1, 1, 1]})
    gltf.add("nodes", {"name": "Marker"})
    gltf.add("nodes", {"name": "Plain", "mesh": plain, "translation": [0, 0, -2]})
    gltf.add("scenes", {"nodes": [0, 4]})
    gltf.document["scene"] = 0
    gltf.write_gltf(MODELS / "Hierarchy.gltf")


def alpha_modes():
    """Quads that are opaque, alpha-masked by an embedded PNG, blended, and double-sided."""
    gltf = Gltf()
    leaf = gltf.texture(gltf.embedded_image(encode_png(32, 32, leaf_mask(32)), "image/png"))
    materials = [
        gltf.add("materials", {"name": "Opaque", "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.8, 0.8, 0.3],
                                                                           "metallicFactor": 0}}),
        gltf.add("materials", {"name": "Mask", "alphaMode": "MASK", "alphaCutoff": 0.5, "pbrMetallicRoughness": {
            "baseColorTexture": {"index": leaf}, "metallicFactor": 0}}),
        gltf.add("materials", {"name": "Blend", "alphaMode": "BLEND", "pbrMetallicRoughness": {
            "baseColorFactor": [0.2, 0.4, 1.0, 0.5], "metallicFactor": 0}}),
        gltf.add("materials", {"name": "Double Sided", "doubleSided": True, "pbrMetallicRoughness": {
            "baseColorFactor": [1.0, 0.6, 0.2, 1.0], "metallicFactor": 0}})]
    positions, normals, uvs, indices = quad()
    nodes = []
    for index, material in enumerate(materials):
        mesh = gltf.add("meshes", {"name": gltf.document["materials"][material]["name"],
                                   "primitives": [gltf.mesh_primitive(positions, normals, uvs, indices, material)]})
        nodes.append(gltf.add("nodes", {"name": gltf.document["meshes"][mesh]["name"], "mesh": mesh,
                                        "translation": [(index - 1.5) * 1.2, 0, 0]}))
    gltf.add("scenes", {"nodes": nodes})
    gltf.document["scene"] = 0
    gltf.write_glb(MODELS / "AlphaModes.glb")


def main():
    MODELS.mkdir(parents=True, exist_ok=True)
    ENVIRONMENTS.mkdir(parents=True, exist_ok=True)
    textured_cube()
    material_spheres()
    hierarchy()
    alpha_modes()
    (ENVIRONMENTS / "Sky.hdr").write_bytes(encode_hdr(128, 64, sky(128, 64)))
    (ENVIRONMENTS / "Uniform.hdr").write_bytes(encode_hdr(16, 8, [[(1.0, 1.0, 1.0)] * 16 for _ in range(8)]))

    for name in ("Model", "Image", "Hdr"):
        (CORPUS / name).mkdir(parents=True, exist_ok=True)
    (CORPUS / "Model" / "MaterialSpheres.glb").write_bytes((MODELS / "MaterialSpheres.glb").read_bytes())
    (CORPUS / "Model" / "AlphaModes.glb").write_bytes((MODELS / "AlphaModes.glb").read_bytes())
    (CORPUS / "Model" / "Hierarchy.gltf").write_bytes((MODELS / "Hierarchy.gltf").read_bytes())
    (CORPUS / "Image" / "Checker.png").write_bytes(
        encode_png(16, 16, checker(16, 4, (255, 0, 0, 255), (0, 0, 255, 128))))
    (CORPUS / "Image" / "Checker.jpg").write_bytes(
        encode_jpeg(16, 16, [[200 if ((x // 4) + (y // 4)) % 2 == 0 else 50 for x in range(16)] for y in range(16)]))
    (CORPUS / "Hdr" / "Sky.hdr").write_bytes(encode_hdr(16, 8, sky(16, 8)))
    print("Generated the test assets")


if __name__ == "__main__":
    main()

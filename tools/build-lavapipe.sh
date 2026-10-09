#!/usr/bin/env bash
# Builds lavapipe, Mesa's software Vulkan driver, from source at the version pinned in tools/lavapipe.env, and
# installs it where cmake/Lavapipe.cmake looks for it. Linux only - Windows uses a prebuilt release, which CMake
# downloads (see docs/Decisions/0009-lavapipe.md).
#
# Usage: tools/build-lavapipe.sh [install directory]
#
# Needs: curl, Python 3 with venv, Ninja, pkg-config, glslang 12.2+, and the development files of libdrm, zlib and
# LLVM at the pinned LLVM version, e.g.
#   sudo apt install curl python3-venv ninja-build pkg-config glslang-tools libdrm-dev zlib1g-dev llvm-19-dev
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=lavapipe.env
source "${repository_root}/tools/lavapipe.env"

install_dir="${1:-${repository_root}/.cache/lavapipe/mesa-${MESA_VERSION}-linux}"
work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT

llvm_config="$(command -v "llvm-config-${LLVM_VERSION}" || true)"
if [[ -z "${llvm_config}" ]]; then
	echo "error: llvm-config-${LLVM_VERSION} not found. Install LLVM ${LLVM_VERSION}'s development files (llvm-${LLVM_VERSION}-dev)." >&2
	exit 1
fi

echo "Building lavapipe from Mesa ${MESA_VERSION} into ${install_dir}"

# Mesa's build needs a newer Meson than some distributions ship, plus Mako, PyYAML and packaging
python3 -m venv "${work_dir}/venv"
"${work_dir}/venv/bin/pip" install --quiet --disable-pip-version-check "meson==1.12.1" "mako==1.4.3" "pyyaml==6.0.3" "packaging==26.3"
export PATH="${work_dir}/venv/bin:${PATH}"

archive="${work_dir}/mesa-${MESA_VERSION}.tar.xz"
curl --fail --location --silent --show-error --output "${archive}" "https://archive.mesa3d.org/mesa-${MESA_VERSION}.tar.xz"
echo "${MESA_SOURCE_SHA256}  ${archive}" | sha256sum --check --quiet
tar -xJf "${archive}" -C "${work_dir}"

cat > "${work_dir}/native.ini" <<EOF
[binaries]
llvm-config = '${llvm_config}'
EOF

# Only lavapipe: no window-system platforms (rendering is offscreen), no OpenGL, no other drivers. LLVM is linked
# statically, so the driver doesn't depend on the LLVM installed at runtime
meson setup "${work_dir}/build" "${work_dir}/mesa-${MESA_VERSION}" \
	--native-file "${work_dir}/native.ini" \
	--prefix "${install_dir}" \
	--libdir lib \
	--buildtype release \
	-Dplatforms= \
	-Dvulkan-drivers=swrast \
	-Dgallium-drivers= \
	-Dllvm=enabled \
	-Dshared-llvm=disabled \
	-Dopengl=false \
	-Dgles1=disabled \
	-Dgles2=disabled \
	-Dglx=disabled \
	-Degl=disabled \
	-Dgbm=disabled \
	-Dvulkan-layers= \
	-Dtools= \
	-Dbuild-tests=false \
	-Dvalgrind=disabled \
	-Dlibunwind=disabled \
	-Dlmsensors=disabled \
	-Dzstd=disabled \
	-Dxmlconfig=disabled \
	-Dvideo-codecs=

ninja -C "${work_dir}/build"
rm -rf "${install_dir}"
ninja -C "${work_dir}/build" install

icd="${install_dir}/share/vulkan/icd.d/lvp_icd.x86_64.json"
if [[ ! -f "${icd}" ]]; then
	echo "error: the build didn't install ${icd}" >&2
	exit 1
fi

# Meson writes the driver's absolute path into the manifest. The Vulkan loader resolves a relative path against the
# manifest's directory, so a relative one keeps the installation working wherever it's moved or restored to
python3 - "${icd}" <<'PYTHON'
import json
import os
import sys

manifest_path = sys.argv[1]
with open(manifest_path, encoding="utf-8") as manifest_file:
    manifest = json.load(manifest_file)
library_path = manifest["ICD"]["library_path"]
manifest["ICD"]["library_path"] = os.path.relpath(library_path, os.path.dirname(manifest_path))
with open(manifest_path, "w", encoding="utf-8") as manifest_file:
    json.dump(manifest, manifest_file, indent=4)
    manifest_file.write("\n")
PYTHON

echo "lavapipe installed: ${icd}"

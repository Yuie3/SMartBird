#!/usr/bin/env sh
# Build the PS Vita VPK inside the reusable GXM builder image.
set -eu

IMAGE="${IMAGE:-vita-builder-gxm}"
SMB_SERVER="${SMB_SERVER:-}"
SMB_SHARE="${SMB_SHARE:-}"
SMB_PATH="${SMB_PATH:-}"
SMB_USER="${SMB_USER:-guest}"
SMB_PASSWORD="${SMB_PASSWORD:-}"
SMB_DOMAIN="${SMB_DOMAIN:-}"

docker run --rm --platform linux/amd64 -v "$(pwd)":/src "${IMAGE}" \
  "cmake -S /src -B /src/build/vita -G Ninja \
     -DBUILD_VITA_PLAYER=ON \
     -DCMAKE_TOOLCHAIN_FILE=/usr/local/vitasdk/share/vita.toolchain.cmake \
     -DCMAKE_BUILD_TYPE=Release \
     -DVITA_SMB_SERVER='${SMB_SERVER}' \
     -DVITA_SMB_SHARE='${SMB_SHARE}' \
     -DVITA_SMB_PATH='${SMB_PATH}' \
     -DVITA_SMB_USER='${SMB_USER}' \
     -DVITA_SMB_PASSWORD='${SMB_PASSWORD}' \
     -DVITA_SMB_DOMAIN='${SMB_DOMAIN}' && \
   cmake --build /src/build/vita"

#!/usr/bin/env sh
# Build the reusable Vita GXM builder image with libmpv-gxm and libsmb2.
set -eu

IMAGE="${IMAGE:-vita-builder-gxm}"
LIBSMB2_REF="${LIBSMB2_REF:-libsmb2-6.2}"

docker build \
  --platform linux/amd64 \
  --build-arg "LIBSMB2_REF=${LIBSMB2_REF}" \
  -f docker/vita-builder-gxm.Dockerfile \
  -t "${IMAGE}" \
  .

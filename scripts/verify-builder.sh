#!/usr/bin/env sh
# Smoke-test the reusable builder image's libsmb2 pkg-config metadata and link path.
set -eu

IMAGE="${IMAGE:-vita-builder-gxm}"

docker run --rm --platform linux/amd64 "${IMAGE}" \
  "set -eu; \
   pkg-config --modversion libsmb2; \
   pkg-config --libs --cflags libsmb2; \
   printf '%s\n' \
     '#include <stddef.h>' \
     '#include <stdint.h>' \
     '#include <time.h>' \
     '#include <smb2/smb2.h>' \
     '#include <smb2/libsmb2.h>' \
     'int main(void) {' \
     '  struct smb2_context *ctx = smb2_init_context();' \
     '  if (ctx) smb2_destroy_context(ctx);' \
     '  return ctx ? 0 : 1;' \
     '}' > /tmp/libsmb2-smoke.c; \
   arm-vita-eabi-gcc /tmp/libsmb2-smoke.c -o /tmp/libsmb2-smoke.elf \
     \$(pkg-config --cflags --libs libsmb2)"

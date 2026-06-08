FROM xfangfang/wiliwili_psv_builder:latest-gxm

ARG LIBSMB2_REF=libsmb2-6.2
ARG VITASDK=/usr/local/vitasdk
ENV VITASDK=${VITASDK}

RUN set -eux; \
    apk add --no-cache \
        autoconf \
        automake \
        ca-certificates \
        cmake \
        git \
        libtool \
        make \
        ninja \
        pkgconf

RUN set -eux; \
    git clone https://github.com/sahlberg/libsmb2.git /tmp/libsmb2; \
    cd /tmp/libsmb2; \
    git checkout "${LIBSMB2_REF}"; \
    make -f Makefile.platform clean; \
    make -f Makefile.platform vita_install; \
    pcdir="${VITASDK}/arm-vita-eabi/lib/pkgconfig"; \
    mkdir -p "${pcdir}"; \
    { \
        echo "prefix=${VITASDK}/arm-vita-eabi"; \
        echo 'exec_prefix=${prefix}'; \
        echo 'libdir=${exec_prefix}/lib'; \
        echo 'includedir=${prefix}/include'; \
        echo ''; \
        echo 'Name: libsmb2'; \
        echo 'Description: SMB2/3 userspace client library for PS Vita'; \
        echo "Version: ${LIBSMB2_REF}"; \
        echo 'Libs: -L${libdir} -lsmb2 -lmbedtls -lmbedx509 -lmbedcrypto'; \
        echo 'Cflags: -I${includedir}'; \
    } > "${pcdir}/libsmb2.pc"; \
    { \
        echo "prefix=${VITASDK}/arm-vita-eabi"; \
        echo 'exec_prefix=${prefix}'; \
        echo 'libdir=${exec_prefix}/lib'; \
        echo 'includedir=${prefix}/include'; \
        echo ''; \
        echo 'Name: mbedtls'; \
        echo 'Description: lightweight crypto and SSL/TLS library'; \
        echo 'Version: 2.0.0'; \
        echo 'Libs: -L${libdir} -lmbedtls -lmbedx509 -lmbedcrypto'; \
        echo 'Cflags: -I${includedir}'; \
    } > "${pcdir}/mbedtls.pc"; \
    rm -rf /tmp/libsmb2

ENV PKG_CONFIG_PATH=/usr/local/vitasdk/arm-vita-eabi/lib/pkgconfig

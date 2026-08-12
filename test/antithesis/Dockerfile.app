# Builds bitcoin-core-app with the test automation bridge enabled, then
# assembles a runtime image that also carries the Bombadil CLI and the
# specification it explores the UI against.
#
# Build context is the repository root:
#   docker build -f test/antithesis/Dockerfile.app -t gui-qml-sut .

FROM docker.io/library/debian:bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake curl pkgconf python3 ca-certificates \
        libboost-dev libsqlite3-dev libgl-dev libqrencode-dev \
        qt6-base-dev qt6-tools-dev qt6-l10n-tools qt6-tools-dev-tools \
        qt6-declarative-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# BUILD_APP_TESTS is off because the Antithesis run exercises the app itself
# rather than the unit test binaries.
RUN cmake -B build \
        -DBUILD_APP_TESTS=OFF \
        -DENABLE_TEST_AUTOMATION=ON \
        -DBUILD_DAEMON=ON \
        -DENABLE_IPC=OFF \
    && cmake --build build -j"$(nproc)"


FROM docker.io/library/debian:bookworm

# Runtime Qt plugins only: the image runs headless with software rendering, so
# no GPU driver stack is needed.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates libsqlite3-0 libqrencode4 libgl1 \
        qt6-qpa-plugins \
        qml6-module-qtqml qml6-module-qtqml-models \
        qml6-module-qtqml-workerscript qml6-module-qt-labs-settings \
        qml6-module-qtquick qml6-module-qtquick-window \
        qml6-module-qtquick-layouts qml6-module-qtquick-controls \
        qml6-module-qtquick-dialogs qml6-module-qtquick-templates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/bin/bitcoin-core-app /opt/bitcoin-core-app

# The Bombadil CLI, built by test/antithesis/Dockerfile.bombadil.
COPY --from=bombadil /out/bombadil /usr/local/bin/bombadil

COPY test/antithesis/spec /opt/spec
COPY test/antithesis/test /opt/antithesis/test
COPY test/antithesis/entrypoint.sh /opt/entrypoint.sh
RUN chmod +x /opt/entrypoint.sh /opt/antithesis/test/v1/gui/*.sh

ENV QT_QPA_PLATFORM=offscreen \
    QT_QUICK_BACKEND=software \
    LIBGL_ALWAYS_SOFTWARE=1 \
    HOME=/root

# The application is started by the test command, not by the container, so the
# container itself only has to stay alive.
ENTRYPOINT ["/opt/entrypoint.sh"]

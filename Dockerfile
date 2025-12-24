FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC \
    CUBE_PROG_ROOT=/usr/local/STMicroelectronics/STM32CubeProgrammer \
    STEDGEAI_ROOT=/opt/stedgeai \
    ARM_GCC_DIR=/opt/arm-gnu-toolchain \
    GCC_PATH=/opt/arm-gnu-toolchain/bin

# Install base dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    apt-utils ca-certificates curl unzip xz-utils tar git make cmake build-essential \
    libusb-1.0-0 udev python3 python3-pip python3-dev \
    default-jre-headless pv \
    libx11-6 libx11-xcb1 libxext6 libxrender1 libxi6 libdbus-1-3 \
    libfontconfig1 libfreetype6 libglib2.0-0 libxkbcommon0 libxkbcommon-x11-0 \
    libxcb1 libxcb-util1 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-randr0 libxcb-render0 libxcb-render-util0 libxcb-shape0 \
    libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 \
    && rm -rf /var/lib/apt/lists/*

FROM builder AS install-gcc

ARG ARM_GCC_VERSION=13.3.rel1
ARG ARM_GCC_DIR=/opt/arm-gnu-toolchain
# Install ARM GCC toolchain (curl only, with progress)
RUN set -e && \
    URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_GCC_VERSION}/binrel/arm-gnu-toolchain-${ARM_GCC_VERSION}-x86_64-arm-none-eabi.tar.xz" && \
    echo "Downloading ARM GCC toolchain from: $URL" && \
    curl -fSL --retry 3 --connect-timeout 60 --progress-bar "$URL" -o /tmp/gcc.tar.xz && \
    echo "Extracting ARM GCC toolchain..." && \
    pv /tmp/gcc.tar.xz | tar -xJf - -C /opt && \
    mv /opt/arm-gnu-toolchain-*-arm-none-eabi ${ARM_GCC_DIR} && \
    ln -s ${ARM_GCC_DIR}/bin/* /usr/local/bin/ && \
    rm /tmp/gcc.tar.xz && \
    echo "ARM GCC toolchain installed successfully"

FROM install-gcc AS install-node
# Install Node.js and pnpm (with progress)
RUN curl -fSL --progress-bar https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && npm install -g pnpm@latest \
    && rm -rf /var/lib/apt/lists/*

FROM install-node AS install-cubeprog
# Download and extract STM32CubeProgrammer from zip
ARG CUBEPROG_URL=https://resources.camthink.ai/tools/stm32cubeprg-lin-v2-19-0.zip
RUN set -e && \
    URL="${CUBEPROG_URL}" && \
    echo "Downloading STM32CubeProgrammer from: $URL" && \
    curl -fSL --retry 3 --connect-timeout 60 --progress-bar "$URL" -o /tmp/stm32cubeprg.zip && \
    unzip -q /tmp/stm32cubeprg.zip -d /tmp/cubeprog-extract && \
    INSTALLER=$(find /tmp/cubeprog-extract -name "SetupSTM32CubeProgrammer-*.linux" -type f | head -1) && \
    [ -n "$INSTALLER" ] && [ -f "$INSTALLER" ] || (echo "Error: Installer not found!" && exit 1) && \
    echo "Found installer: $INSTALLER" && \
    chmod +x "$INSTALLER" && \
    "$INSTALLER" -options-template /tmp/options && \
    sed -i "s|^INSTALL_PATH=.*|INSTALL_PATH=${CUBE_PROG_ROOT}|" /tmp/options && \
    echo -e "createDesktopLink=0\nlaunchprogram=0\nkeepLaunchingScript=0" >> /tmp/options && \
    "$INSTALLER" -options /tmp/options -console && \
    cp ${CUBE_PROG_ROOT}/Drivers/rules/*.rules /etc/udev/rules.d/ 2>/dev/null || true && \
    rm -rf /tmp/cubeprog-extract && \
    rm -rf /tmp/stm32cubeprg.zip && \
    echo "STM32CubeProgrammer installed successfully"

FROM install-cubeprog AS install-stedgeai
# Install ST Edge AI using online installer
ENV QT_QPA_PLATFORM=minimal
ARG STEDGEAI_PACKAGE=stedgeai0202.stneuralart
ARG STEDGEAI_URL=https://resources.camthink.ai/tools/stedgeai-lin.zip

# Check architecture and handle accordingly
RUN set -e && \
    # Detect architecture
    ARCH=$(uname -m) && \
    echo "Architecture: $ARCH" && \
    \
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then \
        echo "Installing QEMU for x86_64 emulation support" && \
        apt-get update && \
        # Install QEMU and binfmt support
        apt-get install -y --no-install-recommends qemu-user-static binfmt-support && \
        dpkg --add-architecture amd64 && \
        # Update package lists again to include amd64
        apt-get update && \
        # Install essential x86_64 libraries needed for ST Edge AI installer
        apt-get install -y --no-install-recommends \
            libc6:amd64 \
            libstdc++6:amd64 \
            libgcc1:amd64 \
            libglib2.0-0:amd64 \
            libsm6:amd64 \
            libice6:amd64 \
            libx11-6:amd64 \
            libxext6:amd64 \
            libxrender1:amd64 \
            libxrandr2:amd64 \
            libxfixes3:amd64 \
            libasound2:amd64 \
            libpulse0:amd64 \
            libgomp1:amd64 ; \
        echo "QEMU and x86_64 libraries installation completed" ; \
    fi && \
    \
    URL="${STEDGEAI_URL}" && \
    echo "Downloading ST Edge AI from: $URL" && \
    curl -fSL --retry 3 --connect-timeout 60 --progress-bar "$URL" -o /tmp/stedgeai.zip && \
    unzip -q /tmp/stedgeai.zip -d /tmp/stedgeai-extract && \
    echo "Contents of extracted directory:" && ls -la /tmp/stedgeai-extract && \
    INSTALLER=$(find /tmp/stedgeai-extract -name "stedgeai-linux-onlineinstaller*" -type f | head -1) && \
    [ -n "$INSTALLER" ] && [ -f "$INSTALLER" ] || (echo "Error: stedgeai installer not found!" && ls -la /tmp/stedgeai-extract && exit 1) && \
    echo "Found installer: $INSTALLER" && \
    chmod +x "$INSTALLER" && \
    \
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then \
        echo "Running ST Edge AI installer with QEMU emulation" && \
        timeout 600 /usr/bin/qemu-x86_64-static "$INSTALLER" --root ${STEDGEAI_ROOT} --accept-licenses --confirm-command --default-answer install ${STEDGEAI_PACKAGE} || \
        (echo "ST Edge AI installation failed with QEMU emulation" && exit 1) ; \
    else \
        echo "Running ST Edge AI installer natively" && \
        timeout 300 "$INSTALLER" --root ${STEDGEAI_ROOT} --accept-licenses --confirm-command --default-answer install ${STEDGEAI_PACKAGE} || \
        (echo "ST Edge AI installation failed" && exit 1) ; \
    fi && \
    \
    VERSION_DIR=$(find ${STEDGEAI_ROOT} -maxdepth 1 -type d -name "[0-9]*" | head -1) && \
    [ -n "$VERSION_DIR" ] && [ -d "$VERSION_DIR" ] || (echo "Error: Version directory not found in ${STEDGEAI_ROOT}" && ls -la ${STEDGEAI_ROOT} 2>/dev/null || echo "Directory does not exist" && exit 1) && \
    echo "Found version directory: $VERSION_DIR" && \
    \
    ([ -d "${VERSION_DIR}/Utilities/linux" ] && echo "export PATH=\$PATH:${VERSION_DIR}/Utilities/linux" >> /etc/profile.d/stedgeai.sh || true) && \
    echo "export STEDGEAI_CORE_DIR=${VERSION_DIR}" >> /etc/profile.d/stedgeai.sh && \
    \
    rm -rf /tmp/stedgeai-extract 2>/dev/null || true && \
    rm -rf /tmp/stedgeai.zip 2>/dev/null || true && \
    echo "ST Edge AI installation completed"

FROM install-stedgeai AS final
# Set PATH
ENV PATH="${PATH}:${GCC_PATH}:${CUBE_PROG_ROOT}/bin"

# Cleanup and set working directory
RUN rm -rf /tmp/*
WORKDIR /workspace

# Copy entrypoint script
COPY docker-entrypoint.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["/bin/bash"]

LABEL maintainer="CamThink Development Team" \
      description="NE301 STM32N6570 AI Vision Camera Build Environment" \
      version="1.0"

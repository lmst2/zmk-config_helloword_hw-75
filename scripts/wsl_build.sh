#!/usr/bin/env bash
set -eu
export PATH="${HOME}/.local/bin:${PATH}"

sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
  python3-pip python3-venv git cmake ninja-build gperf ccache \
  dfu-util device-tree-compiler wget xz-utils protobuf-compiler \
  libprotobuf-dev

python3 -m pip install --user -q west 'protobuf>=4'

ROOT="/mnt/e/code/zmk-config_helloword_hw-75"
cd "${ROOT}"

if [[ ! -f .west/config ]]; then
  west init -l config
fi
west update
west zephyr-export
pip install --user -q -r zephyr/scripts/requirements.txt

# Zephyr SDK (ARM) — required for STM32 build
SDK_VER="0.16.8"
SDK_DIR="${HOME}/zephyr-sdk-${SDK_VER}"
if [[ ! -d "${SDK_DIR}" ]]; then
  cd "${HOME}"
  SDK_TAR="zephyr-sdk-${SDK_VER}_linux-x86_64.tar.xz"
  if [[ ! -f "${SDK_TAR}" ]]; then
    wget -q --show-progress "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${SDK_VER}/${SDK_TAR}"
  fi
  tar xf "${SDK_TAR}"
  "./zephyr-sdk-${SDK_VER}/setup.sh" -t arm-zephyr-eabi -h -c
fi

export ZEPHYR_SDK_INSTALL_DIR="${SDK_DIR}"
cd "${ROOT}"
export PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python

west build -s zmk/app -b hw75_keyboard@1.2 -d build -- \
  -DZMK_CONFIG="${ROOT}/config" \
  -DKEYMAP_FILE="${ROOT}/config/hw75_keyboard.keymap"

echo "Build OK: ${ROOT}/build/zephyr/zmk.bin"

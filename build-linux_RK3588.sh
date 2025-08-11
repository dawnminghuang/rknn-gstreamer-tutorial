set -e

TARGET_SOC="rk3588"
GCC_COMPILER=aarch64-linux-gnu

export TOOL_CHAIN=/home/alientek/rk3568_linux5.10_sdk/buildroot/output/rockchip_atk_dlrk3568/host/bin/aarch64-buildroot-linux-gnu
export SYS_ROOT=/home/alientek/rk3568_linux5.10_sdk/buildroot/output/rockchip_atk_dlrk3568/host/aarch64-buildroot-linux-gnu/sysroot
export SYSROOT_PATH=${SYS_ROOT}
export PKG_CONFIG_PATH=${SYS_ROOT}/usr/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=${SYSROOT_PATH}
export CC=${TOOL_CHAIN}-gcc
export CXX="${TOOL_CHAIN}-g++ -g"
export CFLAGS="-g"
export CXXFLAGS="-g"

ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd )

# build
BUILD_DIR=${ROOT_PWD}/build/build_linux_aarch64

rm -rf ${BUILD_DIR}
if [[ ! -d "${BUILD_DIR}" ]]; then
  mkdir -p ${BUILD_DIR}
fi

cd ${BUILD_DIR}
cmake ../.. -DCMAKE_SYSTEM_NAME=Linux -DTARGET_SOC=${TARGET_SOC} -DCMAKE_SYSROOT=${SYSROOT_PATH} -DCMAKE_BUILD_TYPE=Debug
make -j4
make install
cd -

#!/bin/sh
# The shader compilers the 3D renderer's build needs (scripts/gpu_shaders.cmake), on a CI runner:
# glslangValidator everywhere, spirv-cross for Metal on macOS. Linux and Windows from the Vulkan
# SDK, pinned (Ubuntu 22.04's glslang 11.8 rejects the bakes' kernels), with VULKAN_SDK handed to
# later steps; macOS from Homebrew.
set -eu
vulkan_sdk=1.4.363.0
case "$(uname -s)" in
    Linux*)
        archive="$RUNNER_TEMP/vulkansdk.tar.xz"
        curl -fsSL -o "$archive" "https://sdk.lunarg.com/sdk/download/$vulkan_sdk/linux/vulkansdk-linux-x86_64-$vulkan_sdk.tar.xz"
        tar -xJf "$archive" -C "$RUNNER_TEMP" --wildcards "*/x86_64/bin/glslang*" "*/x86_64/bin/spirv-cross"
        rm "$archive"
        echo "VULKAN_SDK=$RUNNER_TEMP/$vulkan_sdk/x86_64" >> "$GITHUB_ENV"
        ;;
    Darwin*)
        brew install glslang spirv-cross
        ;;
    MINGW*|MSYS*|CYGWIN*)
        installer="$RUNNER_TEMP/vulkansdk.exe"
        curl -fsSL -o "$installer" "https://sdk.lunarg.com/sdk/download/$vulkan_sdk/windows/vulkansdk-windows-X64-$vulkan_sdk.exe"
        root="C:\\VulkanSDK\\$vulkan_sdk"
        "$installer" --root "$root" --accept-licenses --default-answer --confirm-command install
        echo "VULKAN_SDK=$root" >> "$GITHUB_ENV"
        ;;
    *)
        echo "No shader tools known for $(uname -s)" >&2
        exit 1
        ;;
esac

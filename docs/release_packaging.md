# Release 打包说明

本文档记录 ImZiv 参考 ImHex 的 Windows release 打包方式。

## 参考 ImHex 的做法

ImHex 的 Windows portable 包不是直接压缩构建目录，而是：

1. 用 CMake 配置 release 构建。
2. 执行构建。
3. 执行 `cmake --install`，把可执行文件、资源和运行时依赖安装到干净目录。
4. 把 install 目录作为 portable ZIP 的内容。

这样可以避免把 `CMakeFiles`、`_deps`、`CMakeCache.txt`、`Makefile` 等构建中间产物打进 release 包。

ImZiv 也采用同样思路：`dist/release/ImZiv-<version>-Windows-<arch>/` 是干净的便携版目录，ZIP 只包含这个目录。

## 一键打包

在项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -Force
```

默认参数：

- 构建目录：`build-release`
- 构建类型：`Release`
- CMake generator：`MinGW Makefiles`
- OpenCV：`D:/Project/opencv-mingw/x64/mingw/lib`
- 输出目录：`dist/release`

输出示例：

```text
dist/release/ImZiv-1.0.0-Windows-x86_64/
dist/release/ImZiv-1.0.0-Windows-x86_64.zip
```

如果不希望覆盖已有 release 产物，去掉 `-Force`。已有同名目录或 ZIP 时脚本会停止。

## 手工打包命令

如需手工执行，可以按以下步骤：

```powershell
cmake -S . -B build-release -G "MinGW Makefiles" `
  "-DCMAKE_BUILD_TYPE=Release" `
  "-DCMAKE_INSTALL_PREFIX=dist/release/ImZiv-1.0.0-Windows-x86_64" `
  "-DOpenCV_DIR=D:/Project/opencv-mingw/x64/mingw/lib"

cmake --build build-release
cmake --install build-release

Compress-Archive `
  -Path dist\release\ImZiv-1.0.0-Windows-x86_64 `
  -DestinationPath dist\release\ImZiv-1.0.0-Windows-x86_64.zip
```

注意：PowerShell 中 `-DCMAKE_INSTALL_PREFIX=...ImZiv-1.0.0...` 这类参数建议加引号，否则包含点号和连字符的值可能被错误拆分。

## ZIP 内容

当前 Windows portable ZIP 应包含：

```text
ImZiv.exe
assets/
libopencv_world4140.dll
opencv_videoio_ffmpeg4140_64.dll
libgcc_s_seh-1.dll
libstdc++-6.dll
libwinpthread-1.dll
```

其中：

- `assets/` 包含图标和字体。
- OpenCV DLL 来自 `OpenCV_DIR/../bin`。
- MinGW 运行时 DLL 来自当前 C++ 编译器所在目录。
- 系统 DLL 例如 `KERNEL32.dll`、`USER32.dll`、`OPENGL32.dll` 不打包。

## 验证

构建后建议检查 ZIP 内容：

```powershell
tar -tf dist\release\ImZiv-1.0.0-Windows-x86_64.zip
```

应确认 ZIP 中没有以下构建中间产物：

```text
CMakeFiles/
_deps/
CMakeCache.txt
cmake_install.cmake
CPackConfig.cmake
Makefile
```

可用 `objdump` 检查可执行文件依赖：

```powershell
objdump -p dist\release\ImZiv-1.0.0-Windows-x86_64\ImZiv.exe | rg "DLL Name"
```

需要随包携带的非系统依赖应能在 release 目录里找到。

## CPack

CMake 中已配置 `CPACK_GENERATOR` 为 `ZIP`，因此也可以在构建目录执行：

```powershell
cd build-release
cpack
```

日常本地打包优先使用 `scripts/package_release.ps1`，因为脚本会固定输出路径并执行完整的 configure、build、install、zip 流程。

## GitHub Actions 自动打包

仓库提供 `.github/workflows/release-package.yml`：

- `push` 到 `master` / `main`、创建 `v*` tag、PR、或手动 `workflow_dispatch` 都会触发 Windows x86_64 portable 打包。
- workflow 使用 MSYS2 安装 MinGW、CMake、Make 和 OpenCV。
- 打包命令复用本地脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1 -OpenCVDir "C:/msys64/mingw64/lib/cmake/opencv4" -Force
```

- 生成的 `dist/release/*.zip` 会上传为 GitHub Actions artifact。
- 如果触发源是 `v*` tag，还会把 ZIP 上传到对应 GitHub Release。

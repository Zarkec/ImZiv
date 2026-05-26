# ImZiv

ImZiv 是一个使用 ImGui、GLFW、OpenGL3 和 OpenCV 实现的图片查看器，目标是复刻 ziv 图片查看器的核心体验，并扩展测量、取色和节点式图片处理工作流。

## 功能

- 打开图片，支持启动参数、Ctrl+O 和拖放。
- 滚轮缩放，以鼠标位置为缩放中心。
- 左键、右键或中键拖拽平移。
- 双击或 `F` 适应窗口，`Ctrl+1` 恢复原始大小。
- 左右箭头切换同目录图片。
- 状态栏显示坐标、缩放、尺寸、文件名和采样值。
- 支持测距、角度测量、取色和 HSV 采样。
- 支持基于 ImNodes 的节点式图片处理工作流。

## 构建环境

- Windows
- CMake 3.16+
- MinGW
- OpenCV MinGW 构建版本
- OpenGL 3.0+

项目内置：

- ImGui: `third_party/imgui`
- nlohmann/json: `third_party/nlohmann_json`
- GLFW: 通过 CMake FetchContent 自动下载 v3.4

## 构建

默认 OpenCV 路径为：

```text
D:/Project/opencv-mingw/x64/mingw/lib
```

如果你的 OpenCV 安装路径不同，配置时传入 `OpenCV_DIR`。

```bash
cmake -S . -B build -G "MinGW Makefiles" -DOpenCV_DIR="D:/Project/opencv-mingw/x64/mingw/lib"
cmake --build build
```

运行：

```bash
./build/ImZiv.exe [image_path]
```

## 打包

使用发布脚本生成 Windows 便携包：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -Force
```

输出目录：

```text
dist/release/
```

包名版本来自根目录 `VERSION` 文件，例如：

```text
ImZiv-1.0.0-Windows-x86_64.zip
```

## 项目结构

```text
src/main.cpp            入口
src/app/                应用主循环、菜单和窗口 UI
src/core/               任务管理、内容注册、View 基类等核心工具
src/platform/           原生窗口集成和文件对话框
src/viewer/             图片画布和图片编辑视图
src/tools/              测距、角度、取色等交互工具
src/workflow/           工作流视图、引擎和节点注册
src/workflow/nodes/     工作流节点实现
```

## 开发约定

- 图片加载使用 `cv::imdecode`，保持 Unicode 路径兼容。
- OpenGL 纹理由 `cv::Mat` 从 BGR/BGRA 转 RGBA 后上传。
- Windows 原生能力集中在 `src/platform/`。
- 工作流节点放在 `src/workflow/nodes/`。
- 工作流文件使用 `nlohmann/json` 序列化。
- Commit 提交格式见 `docs/commit_convention.md`，描述部分使用中文。

# ImZiv

使用 ImGui + GLFW + OpenGL3 + OpenCV 复刻 ziv 图片查看器。

## 构建与运行

```bash
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
./ImZiv.exe [image_path]
```

发布包生成：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -Force
```

## 依赖

- **ImGui**: `third_party/imgui`
- **GLFW**: CMake FetchContent 自动下载 v3.4
- **OpenCV**: 默认 `D:/Project/opencv-mingw/x64/mingw/lib`
- **nlohmann/json**: `third_party/nlohmann_json`
- **OpenGL 3.0+**: 系统提供

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

## 功能

- 打开图片，支持 Ctrl+O、拖放和启动参数。
- 滚轮缩放，以鼠标位置为中心。
- 左键、右键或中键拖拽平移。
- 双击或 `F` 适应窗口，`Ctrl+1` 恢复原始大小。
- 左右箭头切换同目录图片。
- 状态栏显示坐标、缩放、尺寸、文件名和采样值。
- 支持测距、角度测量、取色、HSV 采样和工作流式图片处理。

## 编码约定

- 图片加载使用 `cv::imdecode`，保持 Unicode 路径兼容。
- OpenGL 纹理由 `cv::Mat` 从 BGR/BGRA 转 RGBA 后上传。
- Windows 原生能力集中在 `src/platform/`。
- 工作流节点放在 `src/workflow/nodes/`，参数优先使用成员变量和 `drawBody` 控件维护。
- 工作流文件使用 `nlohmann/json` 序列化。
- Commit 提交格式见 `docs/commit_convention.md`，描述部分使用中文。

# Commit 提交格式

本项目提交信息使用 Conventional Commits 风格，并要求描述部分使用中文。

## 格式

```text
<type>: <中文描述>
```

示例：

```text
feat: 添加 Linux 打包流程
fix: 修复图片拖拽加载失败
refactor: 重组源码目录结构
build: 使用 VERSION 文件管理版本
ci: 移除 MSYS2 设置 Action
docs: 添加提交格式说明
```

## 常用 type

- `feat`: 新增功能
- `fix`: 修复 bug
- `refactor`: 重构代码，不改变功能行为
- `build`: 构建系统、依赖、打包脚本相关
- `ci`: GitHub Actions 等 CI/CD 配置
- `docs`: 文档修改
- `style`: 代码格式调整，不影响逻辑
- `test`: 测试相关
- `perf`: 性能优化
- `chore`: 其他维护性改动

## 规则

- `type` 使用英文小写。
- 冒号后保留一个空格。
- 描述使用中文，简明说明本次提交做了什么。
- 不要把多个 type 连在一起，例如不要写 `feat: bugfix: ...`。
- 修复问题使用 `fix:`，不要使用 `bugfix:`。
- 一个提交尽量只表达一类改动；混合改动优先拆分提交。


## 变更内容

-

## 影响范围

- [ ] C++ 核心、数据库或 EDA 集成
- [ ] Python API 或原生绑定
- [ ] 几何、布局、时序、DRC 或评估行为
- [ ] 构建与打包：CMake、wheel、manylinux 或依赖
- [ ] CI 与发布流程
- [ ] 仅测试或文档

## 运行时与打包影响

- [ ] 不影响运行时和打包
- [ ] 修改 C++ 运行时行为或原生 ABI
- [ ] 修改 Python API 或扩展模块行为
- [ ] 修改 wheel 内容或支持的平台
- [ ] 修改 CMake、编译器、依赖或工具链要求

说明：

-

## 验证

- [ ] C/C++ 格式检查：`uvx prek run --config .pre-commit-config.yaml --files <变更文件>`
- [ ] 原生构建：`bash build.sh` 或等价 CMake 命令
- [ ] wheel 构建：`uv build --wheel --no-build-isolation --verbose`
- [ ] Python API 集成测试
- [ ] 针对性测试或冒烟测试
- [ ] 其他：

未执行的检查及原因：

-

## 检查清单

- [ ] 修改范围限定在 ecc-tools。
- [ ] 已说明 C++/Python 公共接口或打包影响。
- [ ] 依赖变更时已更新 `uv.lock`。
- [ ] 未提交本地缓存、虚拟环境或生成文件。
- [ ] 已说明跳过的验证和剩余风险。

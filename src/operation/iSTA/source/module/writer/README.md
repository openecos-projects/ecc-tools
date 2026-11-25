# 设计格式写出

将已有设计、有效约束或抽取模型转换为外部格式。按三个实际输出合同分组，不提供通用 writer 基类、IO 管理器或 umbrella 链接目标。

| 子目录 / 目标 | 输入与职责 |
| --- | --- |
| sdc / ista_sdc_writer | 有效约束及对象身份 → SDC；SdcWriter 负责约束族和依赖顺序、验证及原子替换，SdcObjectWriter 负责对象引用和数值表示 |
| sdf / ista_sdf_writer | 图/库/配置 → SDF；SdfValueExtractor 选值和输出专用求值，SdfFormatter 转义/格式化，SdfWriter 组织单元、连线、检查项 |
| liberty / ista_liberty_writer | TimingModel 与参考库单位/属性 → Liberty；模型先由 model_extractor 生成 |

SDF 当前保留对输出专用弧的库表求值，以及输出 HOLD/SETUP 配对调整。这些是已有的输出策略，不是完整时序分析，也不能描述为“只打印缓存值”。本次不改变其数值、选择规则或回退行为；参考工具语义另行核实。各 writer 不负责会话初始化、不触发传播、不回写图的传播结果。

`liberty/TimingModel.hpp` 属于数据层，写库不需要模型抽取算法的 include 目录。三个目标独立链接，只允许依赖数据和基础设施；SDC 使用 Tcl 的对象引用支持，Liberty 使用外部格式库，均不是命令 frontend/分析依赖。

SDC 命令入口 `module/sdc/WriteSdc` 共用这里的实现。当前支持 `write_sdc file_name [-nosplit]`，未知或未实现选项明确报错；`-version`、`-compress`、`-include` 尚未实现。以 `-` 开头的文件名使用 `./-name.sdc`，空格或 Tcl 元字符路径作为单个 Tcl 参数。输出不包含 read_sdc/write_sdc/report 等流程命令；序列化失败保留已有文件。

SdfValueExtractor 返回按 condition/input transition 归约的 cell arc 值以及完整 check corner 对；SdfWriter 不再聚合 delay、交换 corner 或选择 non-unate 输入边沿。HOLD 调整实现与求值放在同一 cpp，不再单设 SdfCheckAdjuster 头。

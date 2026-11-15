# iSTA 基础工具

此目录只提供与设计格式和分析阶段无关的设施。

| 目录 | 职责 |
| --- | --- |
| logger | 日志、诊断及错误报告 |
| monitor | 时间、资源使用统计 |
| utility | 字符串、文件等基础操作与现有单例存储 |
| common | 共享数值容差 |

`ista_toolkit` 只聚合 logger、monitor、utility。Architecture.cmake 检查现有 iSTA 目标的传递依赖，禁止 foundation targets 引入 STA 数据、设计读写或计算模块。

设计输入位于 `module/reader`；SDC/SDF/Liberty 输出位于 `module/writer`；约束匹配属于 `data_manager/sdc`。不要因为某段业务代码被多处调用，就把它放回 toolkit。

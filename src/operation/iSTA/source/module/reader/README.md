# 设计读入

负责将外部设计表示转换为 iSTA 数据，不启动时序分析。四个读取单元属于同一读入阶段，无需再按解析器拆分子目录。

| 文件 | 输入 → 输出 |
| --- | --- |
| ReadNetwork | 当前 IDB 网络 → Network 及供电连接产生的常量约束；过滤 PG 连线、保留信号 IO 边界 |
| ReadHierarchy | Verilog/导入计划 → 层次对象与扁平网络映射 |
| ReadLiberty | 外部 Liberty 库及已用网络 → LibertyLibrary、单位、表、逻辑条件和输出专用弧 |
| ReadParasitics | 已解析 SPEF → Parasitics 的连接、电阻和电容 |

`STAInterface::readDesign` 决定读取次序并调用 DataManager 完成内部连接绑定；reader 借用具体数据对象，保留项目现有外部 IDB 入口。Config 的解释属于 DataManager。SDC 脚本需要执行命令，仍由 module/sdc/SdcReader 负责。

构建目标 `ista_reader` 私有链接外部解析器；不链接 writer、分析阶段或 iSTA 会话。只使用数据模型的调用者不依赖该目标。

`readNetwork(Network&)` 只导入网表。外部配置中的 SDC 路径由 STAInterface 的读设计流程传入约束数据，网络 reader 不再接收或修改 Sdc。

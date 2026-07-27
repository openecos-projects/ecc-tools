# iRCX 功能说明

本文档是重构前的功能逆向梳理，范围覆盖：

- `src/operation/iRCX/api`
- `src/operation/iRCX/source`
- `src/interface/tcl/tcl_ircx/src`

粒度说明：

- 以公开类成员函数为主要单位记录输入、输出和副作用。
- 纯数据结构只记录有行为的公开函数；仅包含 public 字段的 DTO 记录为“数据载体”。
- 成组 getter/setter 在同一行列出函数名，但每个函数的行为边界按公开函数处理。

## 顶层流程

`RCXAPI` 是外部入口，TCL 命令调用 `RCXAPI` 或直接构造工具配置。主线抽取流程为：

1. `RCXConfig::init/parse` 读取 JSON 配置。
2. `Setup::initialize/readCorner/readMapping/adaptDB` 建立 corner、cap table、mapping 和 iDB 适配数据。
3. `Extraction::run` 依次建立拓扑、环境、工艺变化、RC 结果。
4. `Report::dumpSpef` 通过 `SpefDumper` 输出 SPEF。
5. 工具路径包括 `compare_spef`、`plot_spef`、`dump_net_shape`。

## API 与 TCL 入口

| 类 | 公开函数 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `ircx::RCXAPI` | `getInst()` | 无 | 单例引用 | 初始化静态单例 | 提供 API facade。 |
| `ircx::RCXAPI` | `init(config_file)` | 配置文件路径 | `bool` | 初始化全局配置和运行数据 | 调用 setup 流程。 |
| `ircx::RCXAPI` | `run()` | 无 | `bool` | 写入 topology/environment/RC 结果 | 执行抽取主流程。 |
| `ircx::RCXAPI` | `report()` | 无 | `bool` | 写 SPEF 文件 | 调用报告流程。 |
| `ircx::RCXAPI` | `compare_spef(config)` | compare 配置 | `bool` | 读 SPEF、写 compare 报告 | 调用 `CompareSpefTool::run`。 |
| `ircx::RCXAPI` | `dump_net_shape()` | 无 | `bool` | 写 `.shape` 文件 | 调用 `DumpNetShapeTool::run`。 |
| `ircx::RCXAPI` | `plot_spef(config)` | plot 配置 | `bool` | 写 GDS/LYP | 调用 `PlotSpefTool::run`。 |
| TCL command classes | `check()` | TCL option 状态 | `unsigned`/状态码 | 无 | 校验命令行参数。 |
| TCL command classes | `exec()` | TCL option 状态 | `unsigned`/状态码 | 调用 iRCX API 或工具 | 将 TCL 参数转换为 C++ 配置并执行。 |

## 配置与 Flow

| 类 | 公开函数 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `RCXConfig` | `getInst()` | 无 | 单例引用 | 初始化静态单例 | 管理 JSON 配置。 |
| `RCXConfig` | `init(config_file)` | JSON 路径 | `bool` | 写 `_initialized/_config_path` 等配置字段 | 重置后解析配置文件。 |
| `RCXConfig` | `reset()` | 无 | `void` | 清空配置状态 | 恢复默认线程数、输出目录、corner 列表等。 |
| `RCXConfig` | `setInitialized(initialized)` | 布尔值 | `void` | 修改初始化标志 | 手动设置配置状态。 |
| `RCXConfig` | `initialized/configPath/threadCount/mappingFile/corners/outputDir/reportGeometry()` | 无 | 对应配置值 | 无 | 返回配置字段。 |
| `RCXConfig` | `parse(json_file)` | JSON 路径 | `bool` | 填充配置字段 | 读取 JSON 并解析线程数、mapping、corner、report 配置。 |
| `Setup` | `initialize(config)` | JSON 路径 | `bool` | 填充 `RCXConfig/RCXData` | 读取配置，循环读取 corner、mapping，并适配 DB。 |
| `Setup` | `readCorner(corner_name, temperature, itf_file, captab_file)` | corner 名、温度、ITF、captab | `bool` | 追加 `RCXData::CornerData`、注册 process layer | 读取 ITF 和 captab，建立工艺 corner。 |
| `Setup` | `readCorner(corner_name, itf_file, captab_file)` | corner 名、ITF、captab | `bool` | 同上 | 使用默认温度读取 corner。 |
| `Setup` | `readMapping(mapping_file)` | mapping 路径 | `bool` | 填充 `MappingBuilder` 和 `LayerTable` 映射 | 读取 design/process layer 对照。 |
| `Setup` | `adaptDB()` | 无 | `bool` | 从 iDB 写入 `LayoutData/LayerTable/SpefContext` | 调用 `IdbAdapter`。 |
| `Extraction` | `run()` | 无 | `bool` | 构建抽取中间数据与结果 | 顺序调用 topology、environment、variation、parasitic 计算。 |
| `Report` | `dumpSpef()` | 无 | `bool` | 写 SPEF | 配置 `SpefDumper` 并输出各 corner SPEF。 |

## 数据库与共享数据模型

| 类/结构 | 公开函数 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `LayoutData` | `clear()` | 无 | `void` | 清空设计名、die、routing layer、net、special net | 重置 iDB 适配后的版图数据。 |
| `LayoutData` | `regular_net_count()` | 无 | net 数量 | 无 | 返回 regular net 数。 |
| `LayoutData` | `empty()` | 无 | `bool` | 无 | 判断 regular/special net 数据是否为空。 |
| `Pin` | `is_port()` | 无 | `bool` | 无 | 根据名称是否含 `:` 区分 port 与 instance pin。 |
| `Pin` | `instance_name()` | 无 | 字符串 | 无 | 返回 `inst:pin` 的 instance 部分。 |
| `Pin` | `instance_pin_name()` | 无 | 字符串 | 无 | 返回 `inst:pin` 的 pin 部分。 |
| `Pin` | `port_name()` | 无 | 字符串 | 无 | 返回 port 名。 |
| `RoutingLayer` | `setLayerId/layer_id` | layer id | `void`/id | 修改或读取字段 | 记录 design routing layer id。 |
| `RoutingLayer` | `setLayerName/layer_name` | layer name | `void`/引用 | 修改或读取字段 | 记录 design routing layer 名。 |
| `RoutingLayer` | `setLayerWidth/layer_width` | width | `void`/DBU | 修改或读取字段 | 记录 layer width。 |
| `RoutingLayer` | `setPreferHorz/is_prefer_horz` | 布尔值 | `void`/布尔值 | 修改或读取字段 | 记录 preferred direction。 |
| `RoutingLayer` | `setTrackInfo/track_info` | track info | `void`/引用 | 修改或读取字段 | 记录 DEF track 参数。 |
| `LayerTable` | `clear/clearDesignLayers/clearProcessLayers/clearMappings` | 无 | `void` | 清空映射表 | 管理 design/process/mapping 三类表。 |
| `LayerTable` | `registerDesignLayer/registerProcessLayer/registerMapping` | layer id/name 或 name 对 | `void` | 写入 map | 建立单域和跨域索引。 |
| `LayerTable` | `copyDesignLayersFrom(other)` | 其他 layer table | `void` | 替换 design layer map | 复制 design layer 信息。 |
| `LayerTable` | `design_id/design_name/design_layers/process_id/process_name/design_to_process_id/process_to_design_id` | name 或 id | id/name/list | 无，缺失时抛 `out_of_range` | 查询 layer 映射。 |
| `SpefContext` | `clear()` | 无 | `void` | 清空 SPEF header/name context | 重置 SPEF 输出上下文。 |
| `CornerNetPool<T>` | `init(corner_num, net_num)` | corner/net 数量 | `void` | 分配连续数组 | 建立 corner x net 稠密存储。 |
| `CornerNetPool<T>` | `clear()` | 无 | `void` | 清空数组和维度 | 释放池内容。 |
| `CornerNetPool<T>` | `empty/corner_num/net_num/size` | 无 | 状态值 | 无 | 返回池状态。 |
| `CornerNetPool<T>` | `at(CornerNetId)` | corner/net id | 元素引用 | 可返回可写引用 | 通过二维索引访问元素。 |
| `TopoNode` | `id/net_id/layer_id/point/shape/is_pin_node/pin_name` | 无 | 字段值或引用 | 无 | 返回 per-net local id、net id、几何和 pin 信息。 |
| `TopoNode` | `setLayerId/setPoint/setShape/setPinName` | 字段值 | `void` | 修改节点字段 | 写入拓扑节点属性。 |
| `TopoEdge` | `id/net_id/is_via/via_name/u/v/layer_id/shape/half_width/width/length/center/line_segment/is_horz/coord/lo/hi` | 无 | 字段值或引用 | 无 | 返回 edge 连接、几何、线段派生属性。 |
| `TopoEdge` | `setViaName/setU/setV/setLayerId/setShape` | 字段值 | `void` | 修改 edge，`setShape` 同步派生几何 | 写入拓扑边属性。 |
| `TopoPool` | `node_pool/edge_pool/special_edge_pool` | 无 | pool 引用 | 可返回可写引用 | 暴露连续节点、regular edge、special edge 池。 |
| `TopoPool` | `node_at/edge_at` | flat id | 元素引用 | 可返回可写引用 | 按全局 flat id 访问。 |
| `TopoPool` | `node_index/edge_index` | 对象引用或 `(net,id)` | flat id | 无 | 转换 per-net local id 与全局 pool 索引。 |
| `TopoPool` | `net_nodes/net_edges` | net id | `span` | 可返回可写 span | 获取某 net 的连续节点或边。 |
| `TopoPool` | `net_node_range/net_edge_range` | net id | offset/count | 无 | 查询 net 在 flat pool 中的范围。 |
| `TopoPool` | `reserve(net_count,total_nodes,total_edges)` | 容量 | `void` | 预分配 pools/ranges | 为并行前构建避免 reallocation。 |
| `TopoPool` | `clear()` | 无 | `void` | 清空所有 pool/range | 重置拓扑池。 |
| `TopoPool` | `addNet(nodes, edges)` | per-net 节点和边 | `void` | 追加到 flat pool，分配 local id | 建立 regular net 拓扑。 |
| `TopoPool` | `addSpecialEdges(edges)` | special edges | `void` | 追加 special pool | 存储不参与 RC graph 的特殊网络几何。 |
| `NetEnvironment` | `appendEdgeIntervals(intervals)` | edge interval 列表 | `void` | 追加一组 interval | 使用 `GroupPool` 存储每 edge 环境。 |
| `NetEnvironment` | `edgeIntervals(edge_id)` | edge id | span | const 访问 | 返回某 edge 的邻接环境切片。 |
| `NetEnvironment` | `clear()` | 无 | `void` | 清空 interval pool | 重置环境数据。 |
| `NetEtchProfile` | `appendEdgeIntervals(intervals)` | edge interval 列表 | `void` | 追加一组 interval | 使用 `GroupPool` 存储每 edge 工艺变化。 |
| `NetEtchProfile` | `edgeIntervals(edge_id)` | edge id | span | 可返回可写 span | 返回某 edge 的 etch/thickness interval。 |
| `NetEtchProfile` | `clear()` | 无 | `void` | 清空 interval pool | 重置工艺变化数据。 |
| `RCTable` | `clear()` | 无 | `void` | 清空 corner/net RC 和 coupling map | 重置计算结果。 |
| `RCTable` | `init(corner_num, net_num, topo)` | 维度和 topology | `void` | 预分配每 corner/net edge 的 R/GCap 数组和 ccap bucket | 为并行计算准备固定容量。 |
| `RCTable` | `corner_net_res_pool/corner_net_gcap_pool` | `CornerNetId` | span | 可返回可写 span | 获取某 corner/net 的 edge resistance 或 ground cap 数组。 |
| `RCTable` | `append_net_ccap_entry(net_id, edge_a, edge_b, corner_id, value)` | coupling entry | `void` | 写入 per-net bucket | 并行计算中按 net 局部积累 coupling cap。 |
| `RCTable` | `merge_net_ccap_entries()` | 无 | `void` | 合并 per-net bucket 到全局 undirected map | 并行循环后归并 coupling cap。 |
| `RCTable` | `corner_num/net_num/merged_ccap` | 无 | 状态或 map 引用 | 无 | 返回结果表维度和合并后的 coupling cap。 |
| `RCXData::CornerData` | ctor/dtor/move/assignment | corner 数据 | 对象生命周期 | 管理 `ProcessCorner` unique_ptr | 持有每个 corner 的 ITF、captab、温度。 |
| `RCXData::CornerData` | `halfNodeScaleFactor()` | 无 | `F64` | 无 | 从 process corner 或默认值返回缩放系数。 |
| `RCXData` | `getInst()` | 无 | 单例引用 | 初始化静态单例 | 全局运行时数据容器。 |
| `RCXData` | `reset()` | 无 | `void` | 清空 layout、corner、topology、RC 等 | 重置抽取运行状态。 |
| `RCXData` | `setDBData(layout_data, design_layer_table, spef_context)` | iDB 适配结果 | `void` | 移入 layout，更新 layer/spef context | 接收 DB 适配数据。 |
| `RCXData` | `layout/spef_context/layer_table/mapping_builder/topo_pool/rc_table/net_env_pools/corner_net_etch_pools/corner_data` | 无 | 引用 | 可返回可写引用 | 暴露共享运行数据。 |
| `RCXData` | `halfNodeScaleFactor(corner_idx)` | corner id | `F64` | 无 | 查询 corner half node scale。 |
| `RCXData` | `hasCorner(corner_name)` | 名称 | `bool` | 无 | 判断 corner 是否存在。 |
| `RCXData` | `setProcessLayersRegistered/processLayersRegistered` | 布尔值或无 | `void`/`bool` | 修改或读取标志 | 记录 process layer 是否已注册。 |
| `IdbAdapter` | `IdbAdapter(idb)` | iDB builder 指针 | 对象 | 保存外部指针 | 建立 iDB 到 iRCX 的适配器。 |
| `IdbAdapter` | `adapt(layout_data, layer_table, spef_context)` | 输出容器引用 | `bool` | 写入 layout/layer/spef context | 从 iDB 提取 design、layer、net、pin、wire、via 和 special net。 |

## 拓扑、环境与工艺变化

| 类 | 公开函数 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `TopologyBuilder` | `TopologyBuilder(topo_pool)` | topology pool 引用 | 对象 | 保存目标 pool 指针 | 建立 topology builder。 |
| `TopologyBuilder` | `buildAll(layout)` | layout | `void` | 清空并填充 `TopoPool` | 为所有 regular nets 和 special nets 建拓扑。 |
| `TopologyBuilder` | `buildSpecial(layout)` | layout | `void` | 写 special edge pool | 将 special net 几何转成 special edges。 |
| `TopologyBuilder` | `buildNet(net)` | layout net | `NetTopo` | 无 | 将 segment/via/pin 转为 per-net node/edge。 |
| `Environment` | `setLayoutData/setTopoPool` | 指针 | `void` | 保存只读依赖 | 注入环境构建输入。 |
| `Environment` | `reset()` | 无 | `void` | 清空 track/pixel/search map | 重置环境 builder。 |
| `Environment` | `buildNetEnvironments(net_environments)` | 输出 vector | `bool` | 填充每 net 的 `NetEnvironment` | 建立 track/pixel 索引并并行计算邻接环境。 |
| `Track` | getters/setters for track/bucket | DBU 值 | `void`/DBU | 修改或读取 track/bucket 参数 | 管理 preferred-direction track bucket。 |
| `Track` | `coordToTrack/coordToBucket` | 坐标 | track/bucket index | 无 | 坐标到索引映射。 |
| `Track` | `initTrack()` | 无 | `bool` | 分配 bucket 容器 | 初始化 track search grid。 |
| `Track` | `addEdge(edge)` | topology edge | `void` | 将 edge 指针加入覆盖 bucket | 建立邻近 edge 搜索索引。 |
| `Track` | `overlap(line_seg, search_track_num, widen_func)` | 查询线段、搜索 track 数、可选扩展函数 | overlap 列表 | 无 | 按 track 方向寻找最近覆盖区间和未覆盖区间。 |
| `PixelOverlap` | `empty()` | 无 | `bool` | 无 | 判断区间是否为空。 |
| `Pixel` | getters/setters for grid | DBU 值 | `void`/DBU | 修改或读取 grid 参数 | 管理非 preferred 方向 occupancy grid。 |
| `Pixel` | `coordToXIdx/coordToYIdx/idxToXCoord/idxToYCoord` | 坐标或索引 | 映射值 | 无 | 坐标与 pixel index 转换。 |
| `Pixel` | `initPixel()` | 无 | `bool` | 分配 occupancy matrix | 初始化 pixel grid。 |
| `Pixel` | `addEdge(edge)` | topology edge | `void` | 标记 edge 覆盖的 pixel | 建立金属覆盖栅格。 |
| `Pixel` | `overlap(line_seg)` | 查询线段 | occupied interval 列表 | 无 | 沿线段扫描 conductor run。 |
| `TrackOverlapMerge` | `compute(...)` | query 区间、上下侧 overlap | 输出 interval vector | 写输出 vector | 归并两侧 track overlap 为 edge environment interval。 |
| `PixelOverlapMerge` | `compute(...)` | query 区间、多层 pixel overlap | 输出 cross/diag interval | 写输出 vector | 归并垂直/斜向 coupling 环境。 |
| `ProcessVariation` | setters | layout/env/etch/layer/topo/corner 指针 | `void` | 保存依赖 | 注入工艺变化输入输出。 |
| `ProcessVariation` | `corner_num()` | 无 | 数量 | 无 | 返回 corner 数。 |
| `ProcessVariation` | `metal_density()` | 无 | 指针 | 无 | 返回已构建 metal density。 |
| `ProcessVariation` | `reset()` | 无 | `void` | 清空内部密度和维度 | 重置工艺变化状态。 |
| `ProcessVariation` | `buildEtchProfiles()` | 无 | `bool` | 填充 `CornerNetPool<NetEtchProfile>` | 初始化 interval，应用 thickness/width variation。 |
| `MetalDensity` | `setTopoPool` | topology 指针 | `void` | 保存依赖 | 注入 topology。 |
| `MetalDensity` | `clear()` | 无 | `void` | 清空 polygon set | 重置密度模型。 |
| `MetalDensity` | `build()` | 无 | `void` | 构建 per-layer polyset | 合并 regular 和 special edge shape。 |
| `MetalDensity` | `cal_density(layer, box)` | layer 和窗口 | `double` | 无 | 计算窗口内金属面积比。 |
| `ThicknessModel` | setters | layout/layer/topo/corner/density 指针 | `void` | 保存依赖 | 注入厚度模型输入。 |
| `ThicknessModel` | `apply_thickness_variation(corner_idx, net_idx, etch_profile)` | corner、net、etch profile | `void` | 修改 etch interval 的 thickness/height | 根据 ITF PBTV/密度模型计算厚度变化。 |
| `WidthModel` | setters | topo/layer/corner 指针 | `void` | 保存依赖 | 注入宽度模型输入。 |
| `WidthModel` | `apply_width_variation(corner_idx, net_idx, etch_profile)` | corner、net、etch profile | `void` | 修改 interval center/width | 使用 conductor etch VWS 查询左右侧 etch 并更新宽度。 |

## RC 计算

| 类/函数 | 公开函数 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `ResistanceCalc` | setters | layout/layer/topo/etch/rc/corner 指针 | `void` | 保存依赖，设置 DBU 换算 | 注入 resistance 计算输入输出。 |
| `ResistanceCalc` | `calc()` | 无 | `bool` | 写 `RCTable` resistance pool | 校验输入，按 corner/net/edge 计算线电阻和 via 电阻。 |
| `WireResistanceModel` | `calc(segment, edge_etch_intervals, corner, layer, operating_temperature)` | 几何段、etch interval、ITF corner/layer、温度 | `F64` | 无 | 对每段区间按 rho/rpsq/thickness/温度折算并累加。 |
| `ViaResistanceModel` | `calc(edge, corner, layer, micron_per_dbu, operating_temperature)` | via edge、ITF corner/layer、单位、温度 | `F64` | 无 | 按 via 面积、RPV/RHO/CRT 和 etch width/length 计算 via 电阻。 |
| `ResistanceTemperatureCoefficients` | `empty()` | 无 | `bool` | 无 | 判断温度系数是否为零。 |
| temperature helpers | `resistanceTemperatureDeratingFactor/applyResistanceTemperatureDerating` | 温度、系数、电阻 | `F64` | 无 | 计算并应用温度 derating。 |
| `CapacitanceCalc` | setters | layout/env/etch/layer/topo/corner/rc 指针 | `void` | 保存依赖，设置 DBU 换算 | 注入 capacitance 计算输入输出。 |
| `CapacitanceCalc` | `calc()` | 无 | `bool` | 写 `RCTable` ground/coupling cap | 校验输入，按 corner/net/edge 并行计算 capacitance。 |
| `CapTableQuery` | ctor | cap table、当前 layer 名 | 对象 | 保存引用 | 建立 captab 查询上下文。 |
| `CapTableQuery` | `nearCap(below_layer, above_layer, spacing)` | 上下层和 spacing | `CapacitanceResult` | 无 | 查询两层或三层近邻 capacitance。 |
| `CapTableQuery` | `farthestCap(below_layer, above_layer)` | 上下层 | `CapacitanceResult` | 无 | 查询 farthest capacitance。 |
| `MakeSideContext` | spacing、邻接 edge、net id | `SideContext` | 无 | 将邻接 edge 分类为 none/special/same/other。 |
| `SideContext` | `occupied/sameNet/specialNet()` | 无 | `bool` | 无 | 查询邻接分类状态。 |
| `EdgeCapAccumulator` | ctor | cap query、topo、rc table、edge gcap span、corner/net/edge id | 对象 | 保存可写目标 | 建立单 edge cap 累加器。 |
| `EdgeCapAccumulator` | `accumulateSpan(span_length, below_layer, above_layer, low_side, high_side)` | 区间长度、上下层、两侧邻接 | `void` | 写 ground cap span 和 per-net ccap entries | 根据 captab 结果折算到当前 span。 |

## SPEF 报告

| 类 | 公开函数 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `SpefDumper` | setters | spef/layout/topo/rc/corner/layer 指针 | `void` | 保存依赖 | 注入 SPEF 输出输入。 |
| `SpefDumper` | `dump(output_dir)` | 输出目录 | `bool` | 写一个或多个 SPEF 文件 | 构建 name map、port、node/net 名称、layer map 后按 corner 输出。 |
| `SpefDumper` | `buildNameMaps/buildPortIo/buildNodeSpefNames/buildNetSpefNames/buildCouplingRefs/buildReportLayerMap` | 无或 corner id | `void` | 填充 mutable report cache | 预处理 SPEF 输出索引。 |
| `SpefDumper` | `nodeName(node)` | topology node | SPEF node name | 无 | 从 pin/cache/坐标生成节点名。 |
| `SpefDumper` | `dumpCorner(output_dir, corner_idx)` | 输出目录、corner id | `bool` | 写单 corner SPEF | 组织 header、name map、ports、D_NET。 |
| `SpefDumper` | `writeHeader/writeNameMap/writePorts/writeLayerMap` | stream、可选 corner id | `void` | 写 stream | 输出 SPEF header 和全局段落。 |
| `SpefDumper` | `writeDNet(os, corner_idx, net_idx)` | stream、corner/net id | `void` | 写 stream | 输出单 net cap/res/connectivity。 |
| `SpefDumper` | `writeNodeGeometry/writeResistanceGeometry` | stream、node/edge、单位 | `void` | 写 stream | 输出几何 annotation。 |
| `SpefDumper` | `reportLayerLevel(design_layer_id)` | design layer id | report layer id | 无 | 映射 SPEF report layer level。 |

## Captab 与 ITF Parser

| 类 | 公开函数 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `parser::CapTable` | `loadFromFile(filePath)` | captab 路径 | `bool` | 填充配置 map | 从文件解析 captab。 |
| `parser::CapTable` | `loadFromString(content)` | 文本 | `bool` | 填充配置 map | 从字符串解析 captab。 |
| `parser::CapTable` | `keys()` | 无 | key 列表 | 无 | 返回所有配置 key。 |
| `parser::CapTable` | `size()` | 无 | 配置数 | 无 | 返回 captab 配置数量。 |
| `parser::CapTable` | `queryTwoLayerCap/queryThreeLayerCap` | layer、上下层、spacing | `CapacitanceResult` | 无 | 对 A/B 系列表按 spacing 插值。 |
| `parser::CapTable` | `queryTwoLayerIsolatedCap/queryThreeLayerIsolatedCap` | layer、上下层 | `CapacitanceResult` | 无 | 返回 isolated/fringe cap。 |
| `parser::CapTable` | `queryTwoLayerFarthestCap/queryThreeLayerFarthestCap` | layer、上下层 | `CapacitanceResult` | 无 | 返回 farthest row ground/coupling cap。 |
| `itf1DLUT` | ctor、`points/keyName/valueName/empty/size` | 可选 name | 表状态 | 无 | 管理一维 LUT 元数据和点集。 |
| `itf1DLUT` | `add_point/setPoints/setKeyName/setValueName/setNames` | 点集或名称 | `void` | 写表并排序 | 设置 LUT 内容。 |
| `itf1DLUT` | `query(index)/query_interpolation(key)` | index 或 key | optional value | 无 | 按索引或 clamp 线性插值查询。 |
| `itf1DLUT` | `clear/operator==` | 无或 rhs | `void`/`bool` | 清空或比较 | 管理表生命周期。 |
| `itf2DLUT` | ctor/copy、`rows/cols/values/rowName/colName/valueName` | 可选 name | 表状态 | 无 | 管理二维 LUT 元数据和矩阵数据。 |
| `itf2DLUT` | `add_row_data/add_col_data/add_value/setRowName/setColName/setValueName/setNames` | 数据或名称 | `void` | 修改表 | 设置 LUT 内容。 |
| `itf2DLUT` | `operator=/operator==` | rhs | 引用/布尔值 | 赋值或比较 | 管理值语义。 |
| `itf2DLUT` | `clear/query/query_interpolation/add_data/setDataList` | index、row/col、list name | optional value 或 `void` | 查询无副作用；add/set 写表 | 支持二维查询、双线性插值和 parser 数据装载。 |
| `itfTitleLut` | ctor、`title/setTitle/setLut/clear` | title/LUT | title 或 `void` | 修改标题或表 | 为二维 LUT 增加 title。 |
| `itfiVia` | getter/setter/query/clear 系列 | via 名称、from/to、area/rpv/crt、etch LUT | 字段值、optional、`void` | setter/clear 修改 ITF via 数据 | 保存 via 工艺参数并按面积或 width/length 查询。 |
| `itfiConductor` | getter/setter/query/clear 系列 | conductor 名、厚度、etch、rho/rpsq、CRT、density、LUT | 字段值、optional、`void` | setter/clear 修改 ITF conductor 数据 | 保存 conductor 工艺参数并提供 rho/rpsq/CRT/etch 查询。 |
| `itfiDielectric` | getter/setter/clear 系列 | dielectric 名、厚度、介电参数 | 字段值或 `void` | 修改 dielectric 数据 | 保存 dielectric 工艺参数。 |
| `Layer` | ctor、`type/id/order/height/name/layerThickness` | layer type | 字段值 | 无 | 抽象 ITF layer wrapper。 |
| `Layer` | `setId/setOrder/setHeight` | 值 | `void` | 修改 wrapper 字段 | 设置 layer id/order/height。 |
| `LayerConductor/LayerDielectric/LayerVia` | ctor、`name/layerThickness` | ITF DTO | 字段值 | 构造时复制 ITF DTO | 将 ITF conductor/dielectric/via 包装成有 layer type/order/height 的对象。 |
| `LayerConductor` | `width/setWidth` | width | `int32_t`/`void` | 修改或读取 width | 保存 DEF/LEF routing width。 |
| `LayerVia` | `botHeight/topHeight/setTopHeight` | top height | `double`/`void` | 修改或读取 via 高度 | 管理 via 上下高度。 |
| `Layers` | `layers/conductorLayers/dielectricLayers/viaLayers` | 无 | vector 引用 | 可返回可写引用 | 管理 owned layer 和分类 raw pointer view。 |
| `Layers` | `uppermostLayerByOrder/lowermostLayerByOrder/find_* /is_lowermost_diel` | order/name/id/height | layer 指针或 bool | 无 | 按 order、name、id、高度查找工艺层。 |
| `Layers` | `addConductorLayer/addDielectricLayer/addViaLayer/clear` | unique_ptr 或无 | `void` | 修改 owned layers 和分类 view | 增删 layer。 |
| `ProcessCorner` | getters/setters | technology、temperature、er、scale、layers map | 字段值或 `void` | 修改 corner 元数据 | 保存单个工艺 corner。 |
| `ProcessCorner` | `update_layers_height()` | 无 | `void` | 更新 layer height | 根据 dielectric/conductor/via 关系计算高度。 |
| `ProcessCorner` | `show_layers()` | 无 | `void` | 写日志/标准输出 | 输出 layer 信息。 |
| `VariationParams` | `addVariationParam(param)` | variation param | `void` | 追加参数 | 保存 ITF variation 参数。 |
| `ItfBuilder` | `itfService()` | 无 | service 指针 | 无 | 返回 ITF service。 |
| `ItfBuilder` | `build(path)` | ITF 路径 | `bool` | 填充 service process corners | 创建 reader 并读取 ITF。 |
| `ItfRead` | `service()` | 无 | service 指针 | 无 | 返回 callback 目标 service。 |
| `ItfRead` | `read(path)` | ITF 路径 | `bool` | 构建 `ProcessCorner` 并加入 service | 注册 callbacks、驱动 legacy ITF parser。 |
| `ItfService` | `processCorners/lastProcessCorner` | 无 | 指针列表或指针 | 无 | 返回 owned corner 的非 owning view。 |
| `ItfService` | `takeProcessCorners/takeLastProcessCorner` | 无 | unique_ptr 或 vector | 转移 ownership | 将解析出的 process corner 移交调用者。 |
| `ItfService` | `addProcessCorner/findProcessCorner` | unique_ptr 或 technology | `void`/指针 | 增加 corner | 管理 corner ownership 和查找。 |
| `MappingBuilder` | `design_to_process_layer_names/process_to_design_layer_names` | 无 | map 引用 | 无 | 返回 mapping 数据。 |
| `MappingBuilder` | `clear/read(mappingPath)` | 无或路径 | `void`/`bool` | 清空或填充 map | 读取 layer mapping 文件。 |

## compare_spef

| 类/结构 | 公开函数 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `CompareSpefTool` | `run(config)` | compare 配置 | `bool` | 读两份 SPEF，写报告 | 校验配置，读取 test/reference，比较并输出。 |
| `compare_spef::Config` | 数据载体 | 文件、阈值、过滤条件、线程数 | 配置字段 | 无 | 保存 compare_spef 参数。 |
| `NetConfigReader` | `read(config)` | 配置引用 | `bool` | 读取并追加 net/pin 过滤条件 | 从 net config 文件补充配置。 |
| `ConfigValidator` | `validate(config)` | 配置 | `bool` | 无 | 校验输入文件、模式、阈值、输出路径。 |
| `SpefReader` | `read(path, data)` | SPEF 路径、输出 data | `bool` | 填充 `Data` | 读取 name map、unit、net、pin、cap/res 数据。 |
| `NodePair` | `ordered(node1,node2)` | 两个节点名 | 有序 pair | 无 | 按字典序规范化 pair。 |
| `NodePair` | `operator</operator==` | rhs | `bool` | 无 | 提供排序和相等比较。 |
| `NodePairHash` | `operator()(pair)` | node pair | hash | 无 | 支持 unordered_map。 |
| `DataIndex` | `reserve/rememberNodeNet/registerNet` | 数量、node/net、net/index | `void` | 写索引 | 建立 net 和 node->net 映射。 |
| `DataIndex` | `containsNet/orderOf/resolveNodeNet` | net 或 node 名 | bool/order/net name | 无 | 查询 net 存在性、顺序和 node owner net。 |
| `CouplingCapStore` | `clear/reserve/add` | count 或 pair/cap | `void` | 写 entries 和 index | 合并跨 net coupling cap。 |
| `CouplingCapStore` | `empty/size/contains/find` | pair 或无 | 状态或 entry 指针 | 无 | 查询 coupling cap store。 |
| `Data` | `reserveNets/findNet/addOrAssignNet` | net 数、net name、net | `void`/指针/引用 | 写 net list 和索引 | 管理单份 SPEF 解析结果。 |
| `Comparator` | ctor | config | 对象 | 初始化 selector/comparator/generator/solver/sorter | 建立比较器。 |
| `Comparator` | `compare(test, reference)` | 两份 SPEF data | `Result` | 无 | 统计 summary，按 net 并行比较 cap/res，排序结果。 |
| `NetSelector` | ctor | config | 对象 | 构建 net filter 集合 | 建立 net 选择器。 |
| `NetSelector` | `selected(net)` | net | `bool` | 无 | 根据 net/pin 配置判断是否比较。 |
| `NetSelector` | `hasPathFilter()` | 无 | `bool` | 无 | 判断是否有 from/to pin 过滤。 |
| `PathPairGenerator` | ctor | config | 对象 | 保存配置 | 建立 p2p pair 生成器。 |
| `PathPairGenerator` | `generate(net)` | SPEF net | node pair 列表 | 无 | 按显式 from/to 或 SPEF-only inference 生成 p2p 路径。 |
| `ResistanceSolver` | `equivalentResistance(net, from_node, to_node)` | net 和两端节点 | optional resistance | 无 | 建图并求等效电阻。 |
| `ResistanceSolver` | `equivalentResistances(net, pairs)` | net 和 pair 列表 | optional resistance 列表 | 无 | 批量求解全部 pair。 |
| `ResistanceSolver` | `equivalentResistances(net, pairs, pair_indices)` | net、pair 列表、索引子集 | optional resistance 列表 | 无 | 对指定 pair 子集求解。 |
| `CouplingCapComparator` | ctor | config | 对象 | 保存阈值配置 | 建立 coupling cap 比较器。 |
| `CouplingCapComparator` | `compare(test, reference, result)` | 两份 data、结果引用 | `void` | 追加 ccap rows/mismatches | 比较跨 net coupling cap。 |
| `ResultSorter` | `sort(result, test, reference)` | 结果和两份 data | `void` | 原地排序结果行 | 统一报告顺序。 |
| `ReportWriter` | ctor | config | 对象 | 保存配置引用 | 建立 compare 报告 writer。 |
| `ReportWriter` | `write(result)` | compare 结果 | `bool` | 写 `summary/tcap/gcap/ccap/p2p` 报告 | 输出文本报告。 |

## plot_spef

| 类/结构 | 公开函数 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `PlotSpefTool` | `run(config)` | plot 配置 | `bool` | 读 SPEF，写 GDS 和 LYP | 校验配置，构建 model，输出可视化文件。 |
| `plot_spef::Config` | `hasNetFilter/hasOutputFilter/plotResistance/plotCouplingCap/plotGroundCap` | 无 | `bool` | 无 | 根据配置判断过滤和输出类型。 |
| `plot_spef::ConfigValidator` | `validate(config)` | 配置 | `bool` | 可创建输出目录 | 校验 SPEF、输出目录、DBU、线程数。 |
| `ModelBuilder` | `build(exchange, config)` | SPEF exchange、配置 | `Model` | 无 | 从 SPEF conn/cap/res/geometry annotation 构建可视化模型。 |
| `GdsWriter` | `write(model, config)` | model、配置 | `bool` | 写 GDS | 将 net/node/edge/cap 转为 GDS 图层。 |
| `GdsWriter` | `formatValue(value, unit)` | 数值、单位 | 字符串 | 无 | 格式化 label。 |
| `LypWriter` | `write(model, config)` | model、配置 | `bool` | 写 KLayout LYP | 生成图层显示配置。 |
| `plot_spef` cap resolver | `ResolveCapacitorEdges(model, config)` | model、配置 | `void` | 原地写入 capacitor 的 edge refs | 将 cap 两端节点解析到相关 resistor edge。 |
| `plot_spef::Model` helpers | `FindNode(model/net, name)` | model 或 net、node 名 | node 指针 | 无 | 根据索引查找节点。 |
| `plot_spef` inline helpers | `ResistorLength/ResistorWidth/IsWireResistor/EdgeRefKey/EdgeRefTieValue/NodeEdgeVoteKey` | resistor、edge ref、node | 数值或 key | 无 | 计算可视化判定和稳定 key。 |

## dump_net_shape

| 类 | 公开函数 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `DumpNetShapeTool` | `run()` | 无 | `bool` | 写 `<design>.shape` | 从 `RCXData::layout/layer_table` 生成 AI-readable net shape 文件。 |

输出格式包括 header、layer order、shape code、net/segment/patch/via/pin records。shape code 语义是类型标签：

- `A`: segment
- `B`: patch
- `C`: via non-cut layer shape
- `D`: via cut layer shape
- `E`: pin non-cut layer shape
- `F`: pin cut layer shape

## 通用 Utils

| 模块 | 公开函数/类型 | 输入 | 输出 | 副作用 | 核心逻辑 |
|---|---|---|---|---|---|
| `GroupPool<T>` | `append/items/groups/item_count/empty/reserve_groups/reserve_items/clear` | item group 或 id | span/状态 | `append/clear/reserve` 修改 pool | 保存变长分组的连续数组。 |
| `parallel` | `ThreadCount(...)` 等 | work item 数、用户 cores | 线程数 | 无 | 统一 OpenMP 线程数策略。 |
| `format` | `fixed/percent` 等 | 数值、精度 | 字符串 | 无 | 统一报告数值格式。 |
| `path` | 路径解析/目录辅助函数 | 路径字符串 | 路径/状态 | 可创建目录 | 统一输出目录和文件名处理。 |
| `string` | `trim/take_token/parse_*` 等 | 字符串 | 字符串视图或 optional 数值 | 无 | 提供轻量文本解析工具。 |
| `interval` | `normalize/overlaps/intersection/midpoint` 等 | 区间端点 | 区间/布尔值 | 可原地规范化 | 统一区间几何操作。 |
| `hash` | pair/point/rect hasher | key | hash | 无 | 支持 unordered 容器。 |
| `StageLog` | ctor、`setSuccess` | stage 名、source location | 对象或 `void` | 析构时写阶段日志 | RAII 阶段日志。 |
| `SourceLocation` | `current/file_name/line` | 可选编译器内建位置 | source location 或字段 | 无 | 记录调用点。 |

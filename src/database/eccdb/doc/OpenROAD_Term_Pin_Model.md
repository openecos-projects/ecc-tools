# OpenROAD 命名下的 Term / Pin 实体模型

2026-09-30：Term 表示逻辑连接，Pin 表示物理接入区域组。以下对象都由相应 EnTT registry 分配独立 entity。

| EccDB 类型 | OpenDB | 含义和持有关系 |
| --- | --- | --- |
| `LibraryCellMaster` | `dbMaster` | 库中的 cell 定义。 |
| `LibraryMasterTerm` | `dbMTerm` | LEF `PIN A` 的逻辑端口；持有有序的 MasterPin ID。 |
| `LibraryMasterPin` | `dbMPin` | 一个 LEF `PORT`；持有多层 geometry 和 via，反向引用所属 MasterTerm。 |
| `DesignInstance` | `dbInst` | DEF component；引用一个 Master，并持有有序的 InstanceTerm ID。 |
| `DesignInstanceTerm` | `dbITerm` | 具体的 `U1/A`；引用 Instance、MasterTerm、普通 Net 和特殊 Net。 |
| `DesignBlockTerm` | `dbBTerm` | 顶层逻辑 I/O；引用 Net，并持有有序的 BlockPin ID。 |
| `DesignBlockPin` | `dbBPin` | 顶层的一个物理 PORT；独立保存 placement、geometry、via 和可选 AP component，反向引用 BlockTerm。 |

```text
LibraryCellMaster ── MasterTerm A ── MasterPin / LEF PORT ── geometry / via
        ↑                   ↑
DesignInstance U1 ── InstanceTerm U1/A ── Net
                                         │
                                     BlockTerm IN
                                         │
                                     BlockPin[] ── geometry / via / AP
```

Net 连接的是 InstanceTerm / BlockTerm。它不直接连接到某个物理 Pin 的某个 RECT。实例接入区域由 MasterTerm 的 MasterPin 提供，再经过 Instance transform 映射到设计坐标；顶层接入区域使用各个 BlockPin 自己的 transform。

## 简单的存储关系

`DesignInstanceTerms::values` 是独立 InstanceTerm 的有序 ID 列表，保持 MasterTerm 顺序。`DesignBlockTerm::pins` 是独立 BlockPin 的有序 ID 列表。两者都不复制子对象的 payload。RECT / POLYGON / VIA 几何仍放普通数组中，不为每个几何值分配 entity。

创建 BlockTerm 时可以同时传入物理 Pin 数据。Storage 为每个 Pin 分配 entity，并维护 `term.pins` 与 `pin.term`。`createBlockPin / updateBlockPin / destroyBlockPin` 支持单个 Pin 的增删改。更新 BlockTerm 的名称、方向等元数据保留已有 Pin ID；删除未连接的 BlockTerm 时级联删除其物理 Pin。

BlockPin 的候选 AP 直接存于该 Pin entity 上的可选 `DesignBlockPinAccessGroups`。AP 不再通过 BTerm 上的第二份 `port_groups` 数组保存。修改该 Pin 的几何/placement 会清除它的 AP；删除一个 Pin 不影响其他 Pin 的 AP。已有 trace 中的 `(BTerm 名称, BPin ordinal)` 在回放边界转为当前数据库的 BlockPin ID。

## API 和兼容名称

Storage/Service 提供 `instanceTerm / findInstanceTerm / instanceTerms`、`blockTerm / blockPins / blockPin`、`masterPins / masterPin` 等名称。已有 `DesignInstancePin`、`DesignIoPin`、`DesignIoPinPort`、`LibraryMasterPort` 和对应 ID 是源码兼容 alias；它们没有另外一套 payload，也没有额外 entity。

InstanceTerm 保存 Instance/MasterTerm 的 runtime pointer，Instance 上保留共享 MasterTerm 名称索引；名称索引不是每个实例各复制一份。按名查询仍是共享 name→ordinal 索引，然后取 InstanceTerm ID。缓存保留在现有热点，未给每条 Wire/Via 增加 pointer。

## 持久化与地址

Design binary schema 升为 6：分别保存 Term、Pin 和 ID 关系。InstanceTerm 的两个 raw pointer、InstanceTerm 列表上的 library/name cache 不写入 snapshot；加载后重建。Instance 和 InstanceTerm 使用 EnTT in-place deletion，其他无关同类对象的删除不会移动它们；禁止对这些 storage 做 sort/compact。Library 在 Design 使用期间保持冻结是现有 cache 前提。

BlockPin 使用普通 EnTT storage，长期关系以 ID 保存。其增删可能移动其他 BlockPin component；Service 返回引用的生命周期与正常 storage 访问一致，不新增长期 BlockPin pointer cache。

## TritonRoute 投影边界

OpenROAD 的 `io::Parser::setBTerms` 对信号端的多个 BPin 报不支持，对电源端的多个 BPin 则将几何聚合到一个 `frBPin`。Native EccDB adaptor 沿用这项算法策略；EccDB 数据库本身保留独立 BPin 的分组与身份。数据库实体模型和 DRT 内部算法表示各自承担不同职责。

本轮改变的是 Term/Pin 实体边界，现有 Wire/Path 存储保持其原有模型。详细 v3 的 34 组 API 回放见 [性能与覆盖范围](results/Term_Pin_Entity_V3_Trace_Replay_20260930.md)；实体生命周期、DEF 和二进制定向测试见 [模型测试报告](results/Term_Pin_Entity_Trace_Replay_20260930.md)。

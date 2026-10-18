# iFP 向 iEMIR 提供供电点文件

## 需求与范围

iFP 完成 PDN 生成后，自动输出供电点文件，供 iEMIR 作为理想电源边界条件输入。这是工具间的数据交付，不是展示报告。

当前约定：以最高电源 stripe 层和次高电源 stripe 层之间的同网 via 交点作为供电点，同时输出电源网和地网。此方案是当前分析所采用的供电假设，不代表这些位置存在真实的 pad、bump 或封装电源连接。

本次实现位于 ecc-tools 的 `fp_main` 分支，不新增 iFP 的 Tcl/Python 参数，不改变原有 PDN 几何，也不修改 iEMIR 的计算流程。

## 输出位置

正常执行 `init_fp`、`run_fp`、`destroy_fp` 即可，无需单独调用导出命令。文件在 `run_fp` 的 PDNGenerator 阶段末尾生成，固定为：

```text
<ifp.temp_directory_path>/pdn_generator/power.ploc
```

`ifp.temp_directory_path` 来自 iFP JSON 配置；相对路径以该配置文件所在目录为基准。每次运行覆盖旧文件，不追加历史供电点。

## 文件格式

纯文本，第一行为表头，每条供电点记录占一行，五列以空白分隔：

```text
# name x_um y_um layer net
VDD_0 100.0 200.0 MET5 VDD
VSS_0 120.0 200.0 MET5 VSS
```

上述坐标仅用于说明格式，实际值由当前设计生成。

| 字段 | 含义 |
| --- | --- |
| `name` | 文件内唯一的供电点名称，格式为 `<net>_<idx>`，每个网从 0 编号 |
| `x_um`、`y_um` | 与设计 DEF 使用同一坐标原点的绝对位置，单位为微米，不是 DBU |
| `layer` | 最高 stripe 层的实际布线层名，不是 cut 层或 via 名称 |
| `net` | 实际电源或地网名，例如 VDD、VSS，也保留其他电源域的原网名 |

文件只提供位置、层和网络，不包含电压、电流、功耗或源阻抗。iEMIR 仍需原有的设计、功耗和工艺等输入。

## 供电点选取规则

1. 从实际生成的 stripe 中确定层集合，按工艺层顺序选择最高和次高两层。不是按名称排序，也不是取 LEF 中未使用的最高层；当前全设计选取一对层，不针对每个电源域另选层。
2. 仅收集已有层间连接流程生成的、属于同一个网的 stripe-to-stripe via 交点。沿用该流程的 via 坐标，不另算一个展示用中心。
3. 记录在较高的 stripe 层。两层即使不在工艺上相邻，也沿用原有跨层连接流程，不额外构造连接。
4. 同网同坐标只输出一次；不同网分别记录。输出按网名、X、Y 排序并编号，保持可重复。
5. 不输出低层 via、rail-to-stripe 连接、macro PG pin 连接、普通负载 pin，或只有几何交叉而未生成 via 的位置。
6. 不足两层 stripe 或两层之间没有符合条件的 via 时，只输出表头，不回退到其他位置，也不保留上次的数据。

只有表头的文件不能作为有效供电输入，iEMIR 现有 reader 会报没有供电点。此时应检查 stripe 和层间连接配置，不应随意补点让分析继续。

## iEMIR 接入

在原有 `init_emir` 调用中，将 `-ploc_file_path` 指向生成的文件。例如，以下变量已由调用脚本配置：

```tcl
set ploc_file [file join $fp_temp_directory_path pdn_generator power.ploc]

init_emir -temp_directory_path $emir_temp_directory_path \
          -instance_power_file_path $instance_power_file_path \
          -technology_file_path $technology_file_path \
          -ploc_file_path $ploc_file
run_emir
destroy_emir
```

- iEMIR 使用的设计必须与该文件对应，保留一致的坐标、层名、PG 网名和电源网络。需要保存 iFP DEF 时，按原流程在 `destroy_fp` 写回 IDB 后执行 `def_save`。
- 不同时指定 `-pad_files` 或启用 `-add_ploc_from_top_def 1`，这几种供电输入方式在当前 iEMIR 中互斥。
- iEMIR 的临时目录应与 iFP 的输出目录分开；不要把供电输入放入 iEMIR 会清理的输出目录。
- 文件能解析，不等于完整 EM/IR 分析已验证。iEMIR 还要确认供电点接触指定层、指定网的实际导体；并且所有需要分析的电源域都应有对应供电输入。
- 若后续需要真实封装供电条件，应替换此交点供电假设。把大量 stripe 交点设置为理想电源会影响压降结果，不能当作封装建模。

## 实现与验收

代码入口：`src/operation/iFP/source/module/pdn_generator/PDNGenerator.cpp`。

- `initPowerLayerPair()` 选择 stripe 层对，`buildLayerConnect()` 收集实际 via 交点，`outputPowerLocation()` 写文件。
- 阶段内数据保存在 `pg_data_manager/PGModel.hpp`；输出使用 FPUTIL 的文件流打开、写入和关闭接口，声明与定义归入 `output` 分区。
- 回归测试为 `test_ifp_pdn_generator`，覆盖层顺序、分网、去重、坐标换算、排除 rail/macro 连接、缺少连接和空文件覆盖。

已有 CMake 构建目录且启用 `BUILD_TESTING` 时，可执行：

```sh
cmake --build build --target test_ifp_pdn_generator ecc_bin -j 16
ctest --test-dir build -R '^test_ifp_pdn_generator$' --output-on-failure
```

已验证一个 GCD 的完整 iFP 流程：生成 18 个供电点，VDD/VSS 各 9 个，位于 MET5，与保存 DEF 中的 MET4-MET5 via 坐标一致；iEMIR 现有 reader 的两种读取入口均可解析。此次验证不包含完整 EM/IR 求解。

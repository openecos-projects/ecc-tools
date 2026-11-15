# 尚未实现的命令

以下名称来自迁移前的纯注释占位文件，没有编译或注册。删除占位源文件不改变当前功能；新增支持时需同时实现约束语义、注册命令及行为测试。

- `add_to_collection`
- `all_connected`
- `all_fanin`
- `all_fanout`
- `all_instances`
- `all_registers`
- `append_to_collection`
- `as_collection`
- `collection_to_list`
- `compare_collections`
- `copy_collection`
- `create_voltage_area`
- `current_instance`
- `delete_clock`
- `delete_generated_clock`
- `filter_collection`
- `foreach_in_collection`
- `get_attribute`
- `get_clock_network_objects`
- `get_hier_clocks`
- `group_path`
- `index_collection`
- `query_objects`
- `read_sdc`
- `remove_case_analysis`
- `remove_clock`
- `remove_clock_exclusivity`
- `remove_clock_groups`
- `remove_clock_jitter`
- `remove_clock_map`
- `remove_clock_sense`
- `remove_clock_transition`
- `remove_data_check`
- `remove_disable_clock_gating_check`
- `remove_disable_timing`
- `remove_drive_resistance`
- `remove_driving_cell`
- `remove_fanout_load`
- `remove_generated_clock`
- `remove_ideal_latency`
- `remove_ideal_network`
- `remove_ideal_transition`
- `remove_input_delay`
- `remove_input_noise`
- `remove_max_area`
- `remove_max_capacitance`
- `remove_max_fanout`
- `remove_max_time_borrow`
- `remove_max_transition`
- `remove_min_capacitance`
- `remove_min_pulse_width`
- `remove_operating_conditions`
- `remove_output_delay`
- `remove_path_group`
- `remove_port_fanout_number`
- `remove_pulse_clock_max_transition`
- `remove_pulse_clock_max_width`
- `remove_pulse_clock_min_transition`
- `remove_pulse_clock_min_width`
- `remove_resistance`
- `remove_wire_load_min_block_size`
- `remove_wire_load_model`
- `remove_wire_load_selection_group`
- `set_clock_gating_check`
- `set_clock_gating_percentage`
- `set_clock_jitter`
- `set_clock_map`
- `set_clock_sense`
- `set_data_check`
- `set_disable_clock_gating_check`
- `set_disable_timing`
- `set_drive`
- `set_drive_resistance`
- `set_fanout_load`
- `set_hierarchy_separator`
- `set_ideal_latency`
- `set_ideal_net`
- `set_ideal_network`
- `set_ideal_transition`
- `set_input_noise`
- `set_level_shifter_strategy`
- `set_level_shifter_threshold`
- `set_logic_dc`
- `set_logic_one`
- `set_logic_zero`
- `set_max_area`
- `set_max_time_borrow`
- `set_min_capacitance`
- `set_min_pulse_width`
- `set_operating_conditions`
- `set_port_fanout_number`
- `set_pulse_clock_max_transition`
- `set_pulse_clock_max_width`
- `set_pulse_clock_min_transition`
- `set_pulse_clock_min_width`
- `set_pvt`
- `set_resistance`
- `set_sense`
- `set_voltage`
- `set_wire_load_min_block_size`
- `set_wire_load_mode`
- `set_wire_load_model`
- `set_wire_load_selection_group`
- `sizeof_collection`
- `sort_collection`
- `unset_case_analysis`
- `unset_clock_groups`
- `unset_clock_transition`
- `unset_data_check`
- `unset_disable_timing`
- `unset_input_delay`
- `unset_output_delay`
- `unset_timing_derate`

## SDC 文件输出

已支持 `write_sdc file_name [-nosplit]`，序列化当前有效约束。
`-version`、`-compress`、`-include` 尚未实现，明确返回错误。
命令实现位于本目录 WriteSdc，序列化实现位于 module/writer/sdc；输出不包含文件读写命令自身。

## 已实现范围和仍未覆盖的行为

`set_max_transition`、`set_max_capacitance` 支持设计、端口、叶 pin、层次 pin、lib_pin 和 clock 的适用限制。层次 pin 映射到连接的物理引脚；clock 集合可限定 `-clock_path/-data_path/-rise/-fall`。Transition 在主库与引脚的 trip-point 基准间换算；capacitance 使用引脚与连线总负载。检查归约为每个物理引脚的最差 slack。Driving cell 的 cap/transition/fanout 限制参与输入端口检查，`-no_design_rule` 禁用此继承。QoR 根据检查结果统计违例。

当前已知限制：通过 `set_case_analysis` 固定为常量的网络可能仍产生 transition 检查项，导致电气报告和 QoR 统计误报违例；`ista_electrical_check` 回归覆盖了这一问题。

`set_timing_derate` 实现单分析上下文的普通 OCV：clock/data、cell/net、rise/fall、early/late、对象作用域、`-increment`、`-cell_check`、minimum period/pulse-width。作用域包括省略对象时的全设计、cell、层次 cell、net、lib_cell；对象与检查种类必须匹配。层次 delay 需明确指定 cell/net 种类。Nominal cache、slew、外部 I/O delay 不被改写；消费者在使用延迟或检查值时应用 factor。`-static` 只用于 net delay；`-dynamic`、SI、AOCV/POCV 不支持，明确报错。

`all_delay_corners` 返回唯一上下文 `default`；`-delay_corner` 校验该非空选择。这是 iSTA 流程方言，并非多 corner 支持。Writer 输出等效单上下文约束。

时钟支持多边沿波形、generated 定义更新、独立 min/max × early/late source-latency 记录和局部 pin/port latency/propagated/uncertainty。单上下文消费 min/early、max/late，对角线以外的注解保留并写出。`reference_pin` 推导与 OCV CRPR 已接入 I/O 路径。内部 pin 作为 I/O delay 目标仍拒绝；没有 latch time-borrowing 模型。Input `-level_sensitive` 在本轮组合/触发器矩阵中没有改变结果，不能由此宣称完整 latch 支持。

单位按主 Liberty 库换算到内部 ns/pF；`set_units` 只检查，不修改单位。当前读库支持 1ns/1ps/1fs、1pF/1fF；其他倍率明确拒绝，避免静默当作 ns/pF。`-multiply_by/-dont_scale` driving-cell 选项拒绝。不可用标准 SDC 保留的 generated `-preinvert` 扩展不能写出。

正常 setter 返回 1，`set_units` 不匹配返回 0 并告警。非法命令/对象或失败写文件保持 `TCL_ERROR`；无效 reference-clock 网络及导致 generated clock 孤立的更新也拒绝。

## 2026-10-24 对象与无时钟路径修复

`set_input_delay/set_output_delay` 省略 clock 时保留无时钟身份，不自动绑定第一个 clock。到达时间继续传播，显式 max/min path-delay 约束仍可评价；普通周期性 setup/hold 需要真实 launch/capture clock。显式 `-clock` 必须解析为一个唯一时钟；失败不改已有 I/O 记录。

`all_clocks`、嵌套查询、集合删除、fanin/fanout 和 `get_object_name/get_full_name` 保留对象类型及 canonical identity。名字输出只接受对象集合；需要文本匹配时先调用 `get_*`。同名的不同类型对象各自输出名字，通配符字面名字不会再次展开。

提供 hierarchy Verilog 时，net 查询使用 elaboration 保存的逻辑网段。`get_pins -of_objects [get_nets ...]` 返回该网段本层的叶引脚及边界引脚；`-leaf` 跨层查找物理叶引脚。负载及普通 net derate 映射到整条物理网；through exception 保留网段路径范围，不扩散到兄弟分支。`fanout` 过滤沿用物理负载数，转义标识符中的 `/` 不视作新的层级。

`get_nets -segments/-boundary_type/-top_net_of_hierarchical_group` 仍未开放。Aliased/feedthrough boundary 的不完整拓扑明确拒绝，不能把这些查询视为已支持。大位宽层次端口的 metadata 建立仍有二次复杂度，尚未完成大规模性能验证。

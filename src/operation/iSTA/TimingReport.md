# 时序报告阅读指南

以下是一份`iSTA`生成的时序报告:

``` 
****************************************
Design : APU
DelayType : max
StartEndType : all
SlackLesserThan : infinity
SlackGreaterThan : -infinity
Nworst : 1
MaxPaths : 1
SortBy : slack
****************************************

  Startpoint: DmaAddr[0]_reg_p
               (rising edge-triggered flip-flop clocked by clk)
  Endpoint: DmaAddr[14]_reg_p
               (rising edge-triggered flip-flop clocked by clk)
  Last common pin: fixio_net_5_25_buf/Y
  Path Group: clk
  Path Type: max

  Point                                    Incr       Path
  ---------------------------------------------------------------
  clock clk (rise edge)                0.0000000000
                                                  0.0000000000
  clock network delay (propagated)     0.1754534737
                                                  0.1754534737
  DmaAddr[0]_reg_p/CK (DFFQX1H7R)      0.0000000000
                                                  0.1754534737 r
  DmaAddr[0]_reg_p/Q (DFFQX1H7R)       0.1479296452
                                                  0.3233831190 r
  _0796_/Y (AND4X1P4H7L)               0.0852407970
                                                  0.4086239160 r
  _0797_/Y (AND4X1P4H7L)               0.0884930111
                                                  0.4971169272 r
  _0825_/Y (NAND4X1P4H7L)              0.0594831308
                                                  0.5566000580 f
  _0831_/Z (NOR2BX1H7R)                0.1188242478
                                                  0.6754243058 r
  _0832_/Y (MUX2X16H7L)                0.0825763042
                                                  0.7580006100 f
  _0833_/Y (OA221X2H7R)                0.0759819711
                                                  0.8339825811 f
  DmaAddr[14]_reg_p/D (DFFQX1H7R)      0.0000000000
                                                  0.8339825811 f
  data arrival time                               0.8339825811

  clock clk (rise edge)                10.0000000000
                                                  10.0000000000
  clock network delay (propagated)     0.1616291230
                                                  10.1616291230
  clock reconvergence pessimism        0.0088542511
                                                  10.1704833741
  DmaAddr[14]_reg_p/CK (DFFQX1H7R)                10.1704833741 r
  library setup time                   -0.0365343127
                                                  10.1339490614
  data required time                              10.1339490614
  ---------------------------------------------------------------
  data required time                              10.1339490614
  data arrival time                               -0.8339825811
  ---------------------------------------------------------------
  slack (MET)                                     9.2999664803


1
```

被`*****`包裹的内容展示了生成报告时的参数设置.
剩下的内容则是每条时序路径的时序报告.

## 报告头部

``` 
  Startpoint: DmaAddr[0]_reg_p
               (rising edge-triggered flip-flop clocked by clk)
  Endpoint: DmaAddr[14]_reg_p
               (rising edge-triggered flip-flop clocked by clk)
  Last common pin: fixio_net_5_25_buf/Y
  Path Group: clk
  Path Type: max
```

报告头部首先展示了时序路径的基础信息, 包含:

- 起点(Startpoint), 终点(Endpoint) , 路径组(Path Group)
- Last Common Pin: 指 发起时钟(Launch Clock) 和 捕获时钟(Capture Clock) 最后一次共同经过的节点
- 时序分析类型(Path Type): 具体的, `MAX` 表示建立时间检查(Setup Check); `MIN` 表示 保持时间检查(Hold Check)

接下来表格中的内容, 说明了时序路径中的具体信息:

## 时钟树延迟部分

```
  Point                                    Incr       Path
  ---------------------------------------------------------------
  clock clk (rise edge)                0.0000000000
                                                  0.0000000000
  clock network delay (propagated)     0.1754534737
                                                  0.1754534737
  DmaAddr[0]_reg_p/CK (DFFQX1H7R)      0.0000000000
                                                  0.1754534737 r
```

我们可以这样去理解一个时钟树:

``` 
clk(时钟源端) ----- 一段组合逻辑(通常是各种BUF) ----> DmaAddr[0]_reg_p/CK(寄存器的时钟端口CK)
```

- `clock clk (rise edge) 0.0000000000 0.0000000000`  
  说明了时钟的上升沿从0时刻开始传播.

- `clock network delay (propagated) 0.1754534737 0.1754534737`  
  说明了时钟树中的组合逻辑产生了`0.1754534737`个时间单位的延迟.
  括号中的`propagated`则说明`STA`计算了时钟树的时延.

- `DmaAddr[0]_reg_p/CK (DFFQX1H7R) 0.0000000000 0.1754534737 r`  
  说明时钟树的信号在在`0.1754534737`时, 到达了`DmaAddr[0]_reg_p/CK`,
  括号中的`DFFQX1H7R`则说明了引脚所属的标准单元, 行尾的`r`则说明到达时, 信号处于上升边沿.
  相应的, 当行尾为`f`时, 说明到达时, 信号处于下降边沿.

以下是一个常见的理想时钟树假设下时序报告的时钟部分:  
在理想时钟树假设下, 时钟会同时到达所有触发器的时钟端口, 且具有无限的驱动能力.

```
  Point                                    Incr       Path
  ---------------------------------------------------------------
  clock clk (rise edge)                0.0000000000
                                                  0.0000000000
  clock network delay (ideal)          0.0000000000
                                                  0.0000000000
  DmaAddr[0]_reg_p/CK (DFFQX1H7R)      0.0000000000
                                                  0.0000000000 r
```

## 数据延迟部分

``` 
  Point                                    Incr       Path
  ---------------------------------------------------------------
  DmaAddr[0]_reg_p/CK (DFFQX1H7R)      0.0000000000
                                                  0.1754534737 r
  DmaAddr[0]_reg_p/Q (DFFQX1H7R)       0.1479296452
                                                  0.3233831190 r
  _0796_/Y (AND4X1P4H7L)               0.0852407970
                                                  0.4086239160 r
  _0797_/Y (AND4X1P4H7L)               0.0884930111
                                                  0.4971169272 r
  _0825_/Y (NAND4X1P4H7L)              0.0594831308
                                                  0.5566000580 f
  _0831_/Z (NOR2BX1H7R)                0.1188242478
                                                  0.6754243058 r
  _0832_/Y (MUX2X16H7L)                0.0825763042
                                                  0.7580006100 f
  _0833_/Y (OA221X2H7R)                0.0759819711
                                                  0.8339825811 f
  DmaAddr[14]_reg_p/D (DFFQX1H7R)      0.0000000000
                                                  0.8339825811 f
  data arrival time                               0.8339825811

```

显然, 电路中信号的传播是需要一定时间的,
表格的这一部分就是把信号从`引脚A`传播到`引脚B`要花多少时间,
信号最终传播到`引脚C`花了多少时间罗列了出来.

表中`Incr`列展示的就是信号传播一步的时间, `Path`列展示的就是信号最终到达要多久, 比如:

`DmaAddr[0]_reg_p/Q (DFFQX1H7R) 0.1479296452 0.3233831190 r`
说明`DmaAddr[0]_reg_p/CK`上升沿的信号, 经过`0.1479296452`个时间单位, 到达了`DmaAddr[0]_reg_p/Q`,
导致了他信号的上升变化.  
而整体而言, `DmaAddr[0]_reg_p/Q`上的信号会在`0.3233831190`时上升.

同样的 `_0796_/Y (AND4X1P4H7L) 0.0852407970 0.4086239160 r`  
说明`DmaAddr[0]_reg_p/Q`的上升沿信号, 经过`0.0852407970`个时间单位, 到达了`_0796_/Y`, 导致了他信号的上升变化.  
而整体而言, `_0796_/Y`上的信号会在`0.4086239160`时上升.

```
  DmaAddr[14]_reg_p/D (DFFQX1H7R) 0.0000000000    0.8339825811 f
  data arrival time                               0.8339825811
```

而最终, 这股信号会在`0.8339825811`时到达另一个寄存器`DmaAddr[14]_reg_p`的`D`端, 到达时为下降沿.

## 时序检查部分

```
  data arrival time                               0.8339825811

  clock clk (rise edge)                10.0000000000
                                                  10.0000000000
  clock network delay (propagated)     0.1616291230
                                                  10.1616291230
  clock reconvergence pessimism        0.0088542511
                                                  10.1704833741
  DmaAddr[14]_reg_p/CK (DFFQX1H7R)                10.1704833741 r
  library setup time                   -0.0365343127
                                                  10.1339490614
  data required time                              10.1339490614
  ---------------------------------------------------------------
  data required time                              10.1339490614
  data arrival time                               -0.8339825811
  ---------------------------------------------------------------
  slack (MET)                                     9.2999664803
```

接下来的内容则是时序检查的部分.  
寄存器在时钟边沿到来并采样数据时, 会对数据的稳定性有要求, 具体而言有:

- `建立时间约束(Setup Constraint)`: 信号必须在时钟边沿到来前稳定一段时间, 也即 **信号不能来的太迟**
- `保持时间约束(Hold Constraint)`: 信号必须在时钟边沿到来后稳定一段时间, 也即 **信号不能撤的太快**
- 我们称这个正在采样的寄存器为**捕获寄存器**

示例中的报告, 则是在对数据做建立时间检查.

`data arrival time 0.8339825811`  
声明了数据的到达时间, 实际上他与捕获寄存器数据端口(通常是D端口)上的到达时间是相同的.  
接下来的`clock clk ... `, `clock network delay` 声明了时钟树中的时延.  
此外 `clock reconvergence pessimism`表示时钟重收敛悲观消除量(CRPR).
由于数据路径和时钟路径在物理上存在共同节点, 工具在计算时会自动加上这个正值, 以抵消过度悲观的估计, 从而得到更真实的时序裕量。

`library setup time -0.0365343127 10.1339490614`  
则声明了捕获寄存器当前的建立时间约束为`0.0365343127`, 而他期望数据在`10.1339490614`之前到达

最后一部分则是**时序裕量(Slack)**的计算, 即捕获寄存器期望的到达时间(`10.1339490614`)减去数据真正的到达时间(
`0.8339825811`), 得到结果`9.2999664803`.  
时序裕量指示了这条路径中的信号能不能正确的被寄存器捕获: 当 **Slack** 大于零时, 说明这条路径是可以正常工作的; 当 **Slack
** 小于零时, 则说明这条路径存在问题.

## 我们应该怎么阅读时序报告?

**检查时序路径是否健康**: 如果只是希望看是不是满足了时序约束, 应该着重关注 **Slack** 的结果, **Slack** 大于零时,
则说明是正常的.
**优化关键路径时**: 通常来说, 时序报告是按照**Slack**的大小升序排列的. 也就是说, 时序越紧张的路径越靠前, 他们也被成为关键路径,
优化他们也是提升芯片频率的关键手段. 此时, 应当着重关注时序路径中的具体情况,终点关注`Incr`极大的单元, 可以考虑更换为更高速的单元,
拥有更强驱动能力的单元, 或则插入寄存器隔断关键路径.
**修复保持违例**: 芯片中的路径也不是说越快越好的, 如果信号传播的太快, 就有可能触发保持违例, 这时候就要观察时序路径中的具体情况.
保持违例通常在后端通过插入延迟单元(如缓冲器)修复, 但设计师仍应关注早期报告中是否存在严重违例, 以便提前调整约束或设计
# Geometry Overview：KLayout 思路与 EccDB 实现

## KLayout 实际做了什么

KLayout 没有把“缩略图”作为持久化的 Shape 表。它把三个问题分开：

1. `Cell` 保存全局和按层 BBox，几何或子实例变化时只标 dirty，更新阶段自底向上重算。
2. 每个 layer/type 有可重建的 `box_tree`，用于 viewport 区域裁剪；查询前才 sort/rebuild dirty tree。
3. GUI redraw worker 对重复的层级 cell 做运行时位图缓存。缓存 key 包含 cell、层级和不含平移的变换，平移只复制 bitmap 并调整像素偏移。很小的图形直接画 BBox，避免展开全部几何。

相关源码在 KLayout：

- `src/db/db/dbCell.{h,cc}`：层级 BBox 与 dirty propagation；
- `src/db/db/dbLayer.h`、`dbBoxTree.h`：按层空间裁剪树；
- `src/laybasic/laybasic/layRedrawThreadWorker.{h,cc}`：cell 位图缓存与小图形 LOD；
- `src/laybasic/laybasic/layLayoutCanvas.{h,cc}`：viewport image cache 和平移复用。

因此，KLayout 的“缩略图”更准确地说是 **BBox culling + LOD + redraw cache**，不是一套重复的持久几何对象。

## EccDB 新增的层

`DesignGeometryOverview` 位于 canonical `GeometryRegistry` 和 GUI 之间：

```text
GeometryRegistry / exact Shape payload
             │
             ▼
DesignGeometrySpatialIndex  -- 精确 R-tree 查询
             │
             ▼
DesignGeometryOverview      -- 稀疏 tile 摘要
             │
             ▼
GUI overview / thumbnail renderer
```

每个 tile 只保存：

- 世界坐标范围；
- tile 内 Shape 数量、覆盖 BBox、覆盖面积估计；
- 每个 layer 的数量和 BBox；
- 16×16 occupancy bit mask；
- 生成该摘要时的 geometry epoch。

它不复制 Shape payload，也不把 Shape ID 列表永久塞进 tile。高缩放或编辑选取时调用 `exactShapes(viewport)` 回到精确 R-tree；低缩放只画 tile 的 BBox、layer 统计或 occupancy mask。

主要 API：

```cpp
auto& overview = design.geometryOverview();
overview.setBaseTileSize(4096);
auto tiles = overview.tiles(viewport, level);
auto exact = overview.exactShapes(viewport);
```

`level` 每增加一级，世界 tile 尺寸扩大一倍，形成稀疏的 LOD 层。tile 只在第一次访问时通过 R-tree 构建，并缓存在内存中；geometry epoch 变化后，重新访问的 tile 才会惰性重建，未受视口影响的 tile 不会被立即扫描。Overview 不进入 binary snapshot，恢复数据库后按需重建。

## 这版的边界

这版先实现了可用的几何 overview 摘要和 occupancy mask，没有伪造 GUI 位图类型，也没有把 KLayout 的 cell cache 误当成数据库对象。当前 epoch 变化后按 tile 惰性重建，保证编辑正确性；后续如果 GUI 需要更低的编辑延迟，可以按旧/新 BBox 只标脏相交 tile，再按 KLayout 的方式向上更新摘要。

R-tree 仍只负责精确候选查询，不能替代 overview，也不能作为缩略图缓存。Shape 的 entity handle、payload 和 owner 关系保持不变，overview 是完全可丢弃的派生层。

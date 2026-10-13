// Regression tests for the Critical memory-safety fixes in basic geometry
// (IdbTree ownership, IdbLayerShape empty/null handling, IdbRect init/arithmetic).
#include "IdbGeometry.h"
#include "IdbLayerShape.h"
#include "IdbTree.h"

#include <cassert>
#include <cstdint>
#include <limits>

namespace {

using namespace idb;

void test_tree_copy_assign_and_self_assign()
{
  Tree a(1);
  Tree b(2);
  Tree c = a;
  assert(c == a);
  assert(c != b);

  b = a;
  assert(b == a);

  b = b;  // self-assignment must be a no-op, not a use-after-free
  assert(b == a);

  Tree empty;
  assert(empty.IsEmpty());
  Tree assigned;
  assigned = empty;
  assert(assigned.IsEmpty());
  assert(assigned == empty);
}

void test_tree_empty_and_invalid_access()
{
  Tree empty;
  assert(empty.Root() == -1);  // no deref of empty _nodes
  assert(empty.Height() == -1);

  Tree t(7);
  Iterator bad(nullptr, nullptr);
  assert(!bad);            // null tree counts as invalid
  assert(*bad == -1);      // no crash on deref
  assert(Tree::IsRoot(bad) == false);
  assert(Tree::isLeaf(bad) == false);
  assert(Tree::NumChildren(bad) == 0);

  Iterator root_it = t.begin();
  assert(*root_it == 7);
  assert(Tree::IsRoot(root_it) == true);
  Iterator parent = Tree::Parent(root_it);  // root has no parent: invalid, not crash
  assert(!parent);
}

void test_tree_composite_ctor_skips_null_and_empty()
{
  Tree empty;
  Tree leaf(3);
  std::list<Tree*> parts;
  parts.push_back(nullptr);
  parts.push_back(&empty);
  parts.push_back(&leaf);

  Tree composite(9, parts);
  assert(!composite.IsEmpty());
  assert(composite.Root() == 9);
}

void test_rect_width_path_always_initializes()
{
  // Diagonal segment with width: must not leave members uninitialized.
  IdbRect diagonal(0, 0, 10, 5, 4);
  assert(diagonal.get_low_x() == 0);
  assert(diagonal.get_low_y() == 0);
  assert(diagonal.get_high_x() == 10);
  assert(diagonal.get_high_y() == 5);

  IdbRect horizontal(10, 0, 0, 0, 4);
  assert(horizontal.get_low_x() == 0);
  assert(horizontal.get_high_x() == 10);

  IdbRect vertical(0, 10, 0, 0, 4);
  assert(vertical.get_low_y() == 0);
  assert(vertical.get_high_y() == 10);
}

void test_rect_midpoint_and_size_avoid_overflow()
{
  IdbRect extreme(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min(),
                  std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max());
  // (min + max) / 2 in int32 is UB; must compute without overflow.
  assert(extreme.get_middle_point_x() == -1);
  assert(extreme.get_middle_point_y() == -1);
  // abs(INT32_MAX - INT32_MIN) overflows int32; must saturate.
  assert(extreme.get_width() == std::numeric_limits<int32_t>::max());
  assert(extreme.get_height() == std::numeric_limits<int32_t>::max());
}

void test_rect_null_pointer_handling()
{
  IdbRect from_null(static_cast<IdbRect*>(nullptr));
  assert(from_null.get_low_x() == 0);
  assert(from_null.get_high_y() == 0);

  IdbRect rect(0, 0, 10, 10);
  assert(rect.containPoint(static_cast<IdbCoordinate<int32_t>*>(nullptr)) == false);
  assert(rect.isIntersection(static_cast<IdbRect*>(nullptr)) == false);
  rect.adjustCoordinate(nullptr, nullptr);  // must not crash
}

void test_layer_shape_empty_and_null_handling()
{
  IdbLayerShape shape;
  assert(shape.get_rect_list_num() == 0);

  IdbCoordinate<int32_t> avg = shape.get_average_xy();  // was: div-by-zero
  assert(avg.get_x() == 0 && avg.get_y() == 0);

  IdbRect bbox = shape.get_bounding_box();  // was: INT_MAX/INT_MIN rect
  assert(bbox.get_low_x() == 0 && bbox.get_low_y() == 0);
  assert(bbox.get_high_x() == 0 && bbox.get_high_y() == 0);

  shape.add_rect(static_cast<IdbRect*>(nullptr));  // must be ignored
  assert(shape.get_rect_list_num() == 0);
  assert(shape.get_rect(0) == nullptr);  // out-of-bounds, was: dead unsigned check
  shape.moveToLocation(nullptr);         // must not crash
}

void test_layer_shape_assign_clears_and_skips_null()
{
  IdbLayerShape src;
  src.add_rect(0, 0, 10, 10);
  src.add_rect(20, 20, 30, 30);

  IdbLayerShape dst;
  dst.add_rect(100, 100, 200, 200);
  dst = src;  // was: appended without clear (leak + duplicates)
  assert(dst.get_rect_list_num() == 2);

  IdbCoordinate<int32_t> avg = dst.get_average_xy();
  assert(avg.get_x() == 15 && avg.get_y() == 15);

  IdbRect bbox = dst.get_bounding_box();
  assert(bbox.get_low_x() == 0 && bbox.get_low_y() == 0);
  assert(bbox.get_high_x() == 30 && bbox.get_high_y() == 30);

  dst = dst;  // self-assignment guard
  assert(dst.get_rect_list_num() == 2);
}

void test_layer_shape_average_uses_wide_accumulator()
{
  IdbLayerShape shape;
  const int32_t big = std::numeric_limits<int32_t>::max() - 1;
  shape.add_rect(0, 0, big, big);
  shape.add_rect(0, 0, big, big);
  shape.add_rect(0, 0, big, big);
  shape.add_rect(0, 0, big, big);
  // 4x midpoints summed in int32 would overflow; int64 must hold.
  IdbCoordinate<int32_t> avg = shape.get_average_xy();
  assert(avg.get_x() == big / 2 && avg.get_y() == big / 2);
}

}  // namespace

int main()
{
  test_tree_copy_assign_and_self_assign();
  test_tree_empty_and_invalid_access();
  test_tree_composite_ctor_skips_null_and_empty();
  test_rect_width_path_always_initializes();
  test_rect_midpoint_and_size_avoid_overflow();
  test_rect_null_pointer_handling();
  test_layer_shape_empty_and_null_handling();
  test_layer_shape_assign_clears_and_skips_null();
  test_layer_shape_average_uses_wide_accumulator();
  return 0;
}

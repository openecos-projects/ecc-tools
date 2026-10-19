// Focused safety checks and compatibility tests against main's geometry behavior.
#include "IdbGeometry.h"
#include "IdbLayerShape.h"
#include "IdbTree.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
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
  assert(empty != a);
  assert(a != empty);
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
  assert(!bad);        // null tree counts as invalid
  assert(*bad == -1);  // no crash on deref
  assert(Tree::IsRoot(bad) == false);
  assert(Tree::isLeaf(bad) == false);
  assert(Tree::NumChildren(bad) == 0);

  Iterator root_it = t.begin();
  assert(*root_it == 7);
  assert(Tree::IsRoot(root_it) == true);
  Iterator parent = Tree::Parent(root_it);  // root has no parent: invalid, not crash
  assert(!parent);
  assert(parent == t.end());
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

void test_rect_axis_aligned_width_preserves_main()
{
  IdbRect horizontal(10, 0, 0, 0, 4);
  assert(horizontal.get_low_x() == 0);
  assert(horizontal.get_high_x() == 10);
  assert(horizontal.get_low_y() == -2 && horizontal.get_high_y() == 2);

  IdbRect vertical(0, 10, 0, 0, 4);
  assert(vertical.get_low_y() == 0);
  assert(vertical.get_high_y() == 10);
  assert(vertical.get_low_x() == -2 && vertical.get_high_x() == 2);

  // Main treats a zero-length segment as vertical, including odd widths.
  IdbRect point(5, 7, 5, 7, 5);
  assert(point.get_low_x() == 3 && point.get_high_x() == 7);
  assert(point.get_low_y() == 7 && point.get_high_y() == 7);
}

void test_rect_preserves_main_arithmetic()
{
  const int32_t coordinates[] = {-101, -3, -2, -1, 0, 1, 2, 3, 101};
  for (int32_t lx : coordinates) {
    for (int32_t ly : coordinates) {
      for (int32_t hx : coordinates) {
        for (int32_t hy : coordinates) {
          IdbRect rect(lx, ly, hx, hy);
          assert(rect.get_middle_point_x() == (lx + hx) / 2);
          assert(rect.get_middle_point_y() == (ly + hy) / 2);
          assert(rect.get_middle_point().get_x() == (lx + hx) / 2);
          assert(rect.get_middle_point().get_y() == (ly + hy) / 2);
          assert(rect.get_width() == std::abs(hx - lx));
          assert(rect.get_height() == std::abs(hy - ly));
        }
      }
    }
  }
}

void test_rect_midpoint_avoids_overflow()
{
  IdbRect extreme(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(),
                  std::numeric_limits<int32_t>::max());
  // The sum is -1: preserve main's truncation toward zero, not floor rounding.
  assert(extreme.get_middle_point_x() == 0);
  assert(extreme.get_middle_point_y() == 0);

  IdbRect positive(std::numeric_limits<int32_t>::max() - 2, 0, std::numeric_limits<int32_t>::max(), 0);
  assert(positive.get_middle_point_x() == std::numeric_limits<int32_t>::max() - 1);
  IdbRect negative(0, std::numeric_limits<int32_t>::min(), 0, std::numeric_limits<int32_t>::min() + 2);
  assert(negative.get_middle_point_y() == std::numeric_limits<int32_t>::min() + 1);

  IdbRect odd(std::numeric_limits<int32_t>::max() - 1, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(),
              std::numeric_limits<int32_t>::min() + 1);
  assert(odd.get_middle_point_x() == std::numeric_limits<int32_t>::max() - 1);
  assert(odd.get_middle_point_y() == std::numeric_limits<int32_t>::min() + 1);
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

  IdbRect bbox = shape.get_bounding_box();  // Keep main's empty-range sentinel.
  assert(bbox.get_low_x() == std::numeric_limits<int32_t>::max());
  assert(bbox.get_low_y() == std::numeric_limits<int32_t>::max());
  assert(bbox.get_high_x() == std::numeric_limits<int32_t>::min());
  assert(bbox.get_high_y() == std::numeric_limits<int32_t>::min());

  shape.add_rect(static_cast<IdbRect*>(nullptr));  // must be ignored
  assert(shape.get_rect_list_num() == 0);
  assert(shape.get_rect(0) == nullptr);  // out-of-bounds, was: dead unsigned check
  shape.moveToLocation(nullptr);         // must not crash
}

void test_layer_shape_preserves_assignment_and_skips_null()
{
  IdbLayerShape src;
  src.add_rect(0, 0, 10, 10);
  src.add_rect(20, 20, 30, 30);

  IdbLayerShape dst;
  dst.add_rect(100, 100, 200, 200);
  dst = src;  // Preserve main's existing append-on-assignment behavior.
  assert(dst.get_rect_list_num() == 3);
  assert(dst.get_rect(0)->get_low_x() == 100);

  IdbCoordinate<int32_t> avg = dst.get_average_xy();
  assert(avg.get_x() == 60 && avg.get_y() == 60);

  IdbRect bbox = dst.get_bounding_box();
  assert(bbox.get_low_x() == 0 && bbox.get_low_y() == 0);
  assert(bbox.get_high_x() == 200 && bbox.get_high_y() == 200);

  dst = dst;  // self-assignment guard
  assert(dst.get_rect_list_num() == 3);
  assert(dst.get_rect(1) != src.get_rect(0));
  src.get_rect_list().push_back(nullptr);
  assert(src.get_average_xy().get_x() == 15);
  IdbLayerShape copy(src);
  assert(copy.get_rect_list_num() == 2);
  assert(copy.get_rect(0) != src.get_rect(0));
}

}  // namespace

int main()
{
  test_tree_copy_assign_and_self_assign();
  test_tree_empty_and_invalid_access();
  test_tree_composite_ctor_skips_null_and_empty();
  test_rect_axis_aligned_width_preserves_main();
  test_rect_preserves_main_arithmetic();
  test_rect_midpoint_avoids_overflow();
  test_rect_null_pointer_handling();
  test_layer_shape_empty_and_null_handling();
  test_layer_shape_preserves_assignment_and_skips_null();
  return 0;
}

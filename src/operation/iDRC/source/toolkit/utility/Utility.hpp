// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "DRCHeader.hpp"
#include "Direction.hpp"
#include "Logger.hpp"
#include "Orientation.hpp"
#include "PlanarRect.hpp"
#include "Rotation.hpp"
#include "json.hpp"

namespace idrc {

#define DRCUTIL (idrc::Utility::getInst())

class Utility
{
 public:
  static void initInst();
  static Utility& getInst();
  static void destroyInst();
  // function

#if 1  // 距离线长计算

  // 获得两坐标的曼哈顿距离
  static int32_t getManhattanDistance(PlanarCoord start_coord, PlanarCoord end_coord);

  // 获得两个矩形的欧式距离
  static double getEuclideanDistance(const PlanarRect& a, const PlanarRect& b);

  static double getProjectionDistance(PlanarRect a, PlanarRect b);

  static int32_t getParallelLength(const PlanarRect& a, const PlanarRect& b);

  static int32_t getOrientEdgeDistance(PlanarRect& a, PlanarRect& b, Orientation orient);

  static int32_t getOrientEnclosure(PlanarRect master, PlanarRect insider, Orientation orient);
#endif

#if 1  // 方向方位计算

  static std::vector<Orientation> getOrientationList(const PlanarCoord& start_coord, const PlanarCoord& end_coord,
                                                     Orientation point_orientation = Orientation::kNone);

  // 判断线段方向 从start到end
  static Orientation getOrientation(const LayerCoord& start_coord, const LayerCoord& end_coord, Orientation point_orientation = Orientation::kNone);

  static Orientation getOppositeOrientation(Orientation orientation);

  static Orientation getCWOrientation(Orientation orientation);

  static Orientation getCCWOrientation(Orientation orientation);

  static std::vector<Orientation> getOrthogonalOrientationList(Orientation orientation);

  // 获取 rect 某个角点向内部的两个方向
  static bool getCornerOrientsInRect(const PlanarRect& rect, const PlanarCoord& corner_point, Orientation& orient1, Orientation& orient2);

  static Direction getOppositeDirection(Direction direction);

  static Orientation getOrientaionFromDirection(Direction direction, bool is_ur);

  // 判断线段方向
  static Direction getDirection(PlanarCoord start_coord, PlanarCoord end_coord);

  // 判断线段是否为一个点
  static bool isProximal(const PlanarCoord& start_coord, const PlanarCoord& end_coord);

  // 判断线段是否为水平线
  static bool isHorizontal(const PlanarCoord& start_coord, const PlanarCoord& end_coord);

  // 判断线段是否为竖直线
  static bool isVertical(const PlanarCoord& start_coord, const PlanarCoord& end_coord);

  // 判断线段是否为斜线
  static bool isOblique(const PlanarCoord& start_coord, const PlanarCoord& end_coord);

  // 判断线段是否为直角线
  static bool isRightAngled(const PlanarCoord& start_coord, const PlanarCoord& end_coord);

#endif

#if 1  // 位置关系计算

  // 三个坐标是否共线
  static bool isCollinear(PlanarCoord& first_coord, PlanarCoord& second_coord, PlanarCoord& third_coord);

  // 是否是凸角位置
  static bool isConvexCorner(Rotation rotation, PlanarCoord& first_coord, PlanarCoord& second_coord, PlanarCoord& third_coord);

  // 是否是凹角位置
  static bool isConcaveCorner(Rotation rotation, PlanarCoord& first_coord, PlanarCoord& second_coord, PlanarCoord& third_coord);

  /**
   * 矩形在矩形内
   *          ________________
   *         |   Master       |
   *         |  ——————————    |
   *         |  |  rect   |   |
   *         |  ——————————    |
   *         |________________|
   */
  static bool isInside(const PlanarRect& master, const PlanarRect& rect);

  // 判断coord是否在rect内,可以选择是否包含边界
  static bool isInside(const PlanarRect& rect, const PlanarCoord& coord, bool boundary = true);

  // 线段在矩形内
  static bool isInside(const PlanarRect& master, const Segment<PlanarCoord>& seg);

  static bool isInside(const PlanarRect& master, const Segment<PlanarCoord>& seg, bool boundary);

  // 线段在线段内
  static bool isInside(const Segment<PlanarCoord>& master, const Segment<PlanarCoord>& seg);

  /**
   *  ！在检测DRC中
   *  如果a与b中有膨胀矩形,那么则用isOpenOverlap
   *  如果a与b中都是真实矩形,那么用isClosedOverlap
   *
   *  isOpenOverlap:不考虑边的overlap
   */
  static bool isOpenOverlap(const PlanarRect& a, const PlanarRect& b);

  static bool isOpenOverlap(const PlanarCoord& start_coord, const PlanarCoord& end_coord, const PlanarRect& rect);

  /**
   *  ！在检测DRC中
   *  如果a与b中有膨胀矩形,那么则用isOpenOverlap
   *  如果a与b中都是真实矩形,那么用isClosedOverlap
   *
   *  isClosedOverlap:考虑边的overlap
   */
  static bool isClosedOverlap(const PlanarRect& a, const PlanarRect& b);

  // 判断两个矩形是否重叠
  static bool isOverlap(const PlanarRect& a, const PlanarRect& b, bool consider_edge = true);

  // abut = 90 / abut < 90 ?
  static bool isDirectionOverlap(const PlanarRect& master, const PlanarRect& rect, Direction direction);

  static Orientation getTouchedEdgeOrient(PlanarRect rect, Segment<PlanarCoord> segment);

  // 判断两个rect在polyset中是否连通
  static bool isRectConnectInPolyset(const GTLPolySetInt& polyset, const PlanarRect& rect1, const PlanarRect& rect2);

#endif

#if 1  // boost数据结构工具函数

#if 1  // int类型

  // 判断gtlpoly 和 gtlrect是否相交
  static bool isPolyRectIntersect(const GTLPolyInt& poly, const GTLRectInt& rect);

  // 区间合并，计算polyset在坐标轴上的投影长度
  static int32_t getProjectionLength(const GTLPolySetInt& ps, int32_t axis);

  // 计算两个rect减去连通部分，剩下矩形的多段PRL
  static int32_t getDirectPRL(const GTLPolySetInt& polyset, const PlanarRect& rect1, const PlanarRect& rect2);

  // 找到polyset和rect最大的prl
  static int32_t getPolysetMaxPRL(const GTLPolySetInt& polyset, PlanarRect rect);

  // 找到polyset在某个矩形内，相对某个方向的边
  static bool isPolysetExternal(const GTLPolySetInt& polyset, PlanarRect rect, Orientation orient);

  static Rotation getRotation(GTLPolyInt& gtl_poly);

  static Rotation getRotation(GTLHolePolyInt& gtl_holy_poly);

  static std::map<Orientation, std::vector<PlanarRect>> getPolyExtEdges(GTLPolySetInt polyset);

  static PlanarCoord convertToPlanarCoord(GTLPointInt gtl_point);

  static PlanarRect convertToPlanarRect(const GTLRectInt& gtl_rect);

  static PlanarRect convertToPlanarRect(const BGRectInt& boost_box);

  static BGRectInt convertToBGRectInt(const PlanarRect& rect);

  static BGRectInt convertToBGRectInt(GTLRectInt& gtl_rect);

  static GTLRectInt convertToGTLRectInt(const PlanarRect& rect);

  static GTLRectInt convertToGTLRectInt(BGRectInt& boost_box);

  static int32_t getLength(BGRectInt& a);

  static int32_t getWidth(BGRectInt& a);

  static PlanarCoord getCenter(BGRectInt& a);

  static BGRectInt enlargeBGRectInt(BGRectInt& a, int32_t enlarge_size);

  static void offsetBGRectInt(BGRectInt& boost_box, PlanarCoord& coord);

  static bool isOverlap(GTLPolySetInt a, GTLRectInt b, bool consider_edge = true);

  static bool isOverlap(BGRectInt& a, BGRectInt& b, bool consider_edge = true);

  static BGRectInt getOverlap(BGRectInt& a, BGRectInt& b);

  static bool isHorizontal(BGRectInt a);

  static int32_t getDiagonalLength(BGRectInt& a);

  static int32_t getEuclideanDistance(BGRectInt& a, BGRectInt& b);

#endif

#endif

#if 1  // idrc数据结构工具函数

  // 获得配置的值
  template <typename T>
  static T getConfigValue(std::map<std::string, std::any>& config_map, const std::string& config_name, const T& default_value)
  {
    T value;
    if (exist(config_map, config_name)) {
      value = std::any_cast<T>(config_map[config_name]);
    } else {
      DRCLOG.warn(Loc::current(), "The config '", config_name, "' uses the default value!");
      value = default_value;
    }
    return value;
  }

  static void printTableList(const std::vector<fort::char_table>& table_list);

#endif

#if 1  // 形状有关计算

  static Segment<PlanarCoord> getReversedSegment(Segment<PlanarCoord> segment);

  static PlanarRect getRect(PlanarCoord start_coord, PlanarCoord end_coord);

  static PlanarRect getRect(Segment<PlanarCoord> segment);

  // 三个点的叉乘
  static int64_t crossProduct(Rotation rotation, PlanarCoord& first_coord, PlanarCoord& second_coord, PlanarCoord& third_coord);

  // 偏移矩形
  static PlanarRect getOffsetRect(PlanarRect rect, PlanarCoord offset_coord);

  static PlanarRect getEnlargedRect(PlanarCoord start_coord, PlanarCoord end_coord, int32_t enlarge_size);

  static PlanarRect getEnlargedRect(PlanarCoord start_coord, PlanarCoord end_coord, int32_t ll_x_minus_offset, int32_t ll_y_minus_offset,
                                    int32_t ur_x_add_offset, int32_t ur_y_add_offset);

  static PlanarRect getRegularRect(PlanarRect rect, PlanarRect border);

  static PlanarRect getEnlargedRect(PlanarCoord center_coord, int32_t enlarge_size);

  static PlanarRect getEnlargedRect(PlanarCoord center_coord, int32_t ll_x_minus_offset, int32_t ll_y_minus_offset, int32_t ur_x_add_offset,
                                    int32_t ur_y_add_offset);

  // 扩大矩形
  static PlanarRect getEnlargedRect(PlanarRect rect, int32_t enlarge_size);

  // 扩大矩形
  static PlanarRect getEnlargedRect(PlanarRect rect, int32_t x_enlarge_size, int32_t y_enlarge_size);

  // 指定方向扩大矩形
  static PlanarRect getEnlargedRect(PlanarRect rect, Orientation orientation, int32_t enlarge_size);

  // 指定方向扩大矩形,但只获得扩大的那一部分
  static PlanarRect getEnlargedPartRect(PlanarRect rect, Orientation orientation, int32_t enlarge_size);

  // 扩大矩形
  static PlanarRect getEnlargedRect(PlanarRect rect, int32_t ll_x_minus_offset, int32_t ll_y_minus_offset, int32_t ur_x_add_offset, int32_t ur_y_add_offset);

  static void minusOffset(PlanarCoord& coord, int32_t x_offset, int32_t y_offset);

  static void addOffset(PlanarCoord& coord, int32_t x_offset, int32_t y_offset);

  static PlanarRect getSpacingRect(PlanarRect a, PlanarRect b);

  // 获得两个矩形的overlap矩形
  static PlanarRect getOverlap(PlanarRect a, PlanarRect b);

  static bool hasShrinkedRect(PlanarRect rect, int32_t shrinked_size);

  static PlanarRect getShrinkedRect(PlanarRect rect, int32_t shrinked_size);

  static PlanarRect getShrinkedRect(PlanarRect rect, int32_t ll_x_add_offset, int32_t ll_y_add_offset, int32_t ur_x_minus_offset, int32_t ur_y_minus_offset);

  static PlanarRect getBoundingBox(const std::vector<PlanarRect>& rect_list);

  // 获得坐标集合的外接矩形
  static PlanarRect getBoundingBox(const std::vector<PlanarCoord>& coord_list);

  // 获得两个矩形的overlap矩形
  static std::vector<PlanarRect> getOverlap(std::vector<PlanarRect> a_rect_list, std::vector<PlanarRect> b_rect_list);

#endif

#if 1  // std数据结构工具函数

  static int32_t getRingIdx(int32_t idx, int32_t size);

  template <typename Key, typename Value>
  static Value getValueByAny(std::map<Key, std::any>& map, const Key& key, const Value& default_value)
  {
    Value value;
    if (exist(map, key)) {
      value = std::any_cast<Value>(map[key]);
    } else {
      value = default_value;
    }
    return value;
  }

  template <typename Key, typename Value>
  static Value getValue(std::map<Key, Value>& map, const Key& key, const Value& default_value)
  {
    Value value;
    if (exist(map, key)) {
      value = map[key];
    } else {
      value = default_value;
    }
    return value;
  }

  template <typename T>
  static T getAverage(const std::vector<T>& value_list)
  {
    if (value_list.empty()) {
      return 0;
    }
    double average = 0;
    for (size_t i = 0; i < value_list.size(); i++) {
      average += value_list[i];
    }
    average /= static_cast<int32_t>(value_list.size());
    if constexpr (std::is_same<T, int32_t>::value) {
      average = std::round(average);
    }
    return T(average);
  }

  template <typename T>
  static void erase(std::vector<T>& list, const std::function<bool(T&)>& erase_if)
  {
    erase(list, erase_if);
  }

  template <typename T, typename EraseIf>
  static void erase(std::vector<T>& list, EraseIf erase_if)
  {
    size_t save_idx = 0;
    size_t sentry_idx = 0;
    while (sentry_idx < list.size()) {
      T& sentry = list[sentry_idx];
      if (!erase_if(sentry)) {
        list[save_idx] = std::move(sentry);
        ++save_idx;
      }
      sentry_idx++;
    }
    list.erase(list.begin() + save_idx, list.end());
  }

  template <typename T>
  static void merge(std::vector<T>& list, const std::function<bool(T&, T&)>& merge_if)
  {
    merge(list, merge_if);
  }

  template <typename T, typename MergeIf>
  static void merge(std::vector<T>& list, MergeIf merge_if)
  {
    size_t save_idx = 0;
    size_t sentry_idx = 0;
    size_t soldier_idx = sentry_idx + 1;
    while (sentry_idx < list.size()) {
      T& sentry = list[sentry_idx];
      while (soldier_idx < list.size()) {
        T& soldier = list[soldier_idx];
        if (!merge_if(sentry, soldier)) {
          break;
        }
        ++soldier_idx;
      }
      list[save_idx] = std::move(sentry);
      ++save_idx;
      if (!(soldier_idx < list.size())) {
        break;
      }
      sentry_idx = soldier_idx;
      soldier_idx = sentry_idx + 1;
    }
    list.erase(list.begin() + save_idx, list.end());
  }

  template <typename T>
  static bool isDifferentSign(T a, T b)
  {
    return a & b ? (a ^ b) < 0 : false;
  }

  static int32_t getFirstDigit(int32_t n);

  static int32_t getDigitNum(int32_t n);

  static int32_t getBatchSize(size_t total_size);

  static int32_t getBatchSize(int32_t total_size);

  static bool isDivisible(int32_t dividend, int32_t divisor);

  static bool isDivisible(double dividend, double divisor);

  template <typename T, typename Compare>
  static void swapByCMP(T& a, T& b, Compare cmp)
  {
    if (!cmp(a, b)) {
      std::swap(a, b);
    }
  }

  template <typename T>
  static void swapByASC(T& a, T& b)
  {
    swapByCMP(a, b, std::less<T>());
  }

  static int32_t getOffset(const int32_t start, const int32_t end);

  template <typename T>
  static std::queue<T> initQueue(const T& t)
  {
    std::vector<T> list{t};
    return initQueue(list);
  }

  template <typename T>
  static std::queue<T> initQueue(std::vector<T>& list)
  {
    std::queue<T> queue;
    addListToQueue(queue, list);
    return queue;
  }

  template <typename T>
  static T getFrontAndPop(std::queue<T>& queue)
  {
    T node = queue.front();
    queue.pop();
    return node;
  }

  template <typename T>
  static void addListToQueue(std::queue<T>& queue, std::vector<T>& list)
  {
    for (size_t i = 0; i < list.size(); i++) {
      queue.push(list[i]);
    }
  }

  template <typename T>
  static void reverseList(std::vector<T>& list)
  {
    reverseList(list, 0, static_cast<int32_t>(list.size()) - 1);
  }

  template <typename T>
  static void reverseList(std::vector<T>& list, int32_t start_idx, int32_t end_idx)
  {
    while (start_idx < end_idx) {
      std::swap(list[start_idx], list[end_idx]);
      start_idx++;
      end_idx--;
    }
  }

  template <typename T>
  static bool isNanOrInf(T a)
  {
    return (std::isnan(a) || std::isinf(a));
  }

  static bool equalDoubleByError(double a, double b, double error);

  template <typename T>
  static bool sameSign(T a, T b)
  {
    return std::signbit(a) == std::signbit(b);
  }

  template <typename T>
  static bool diffSign(T a, T b)
  {
    return !sameSign(a, b);
  }

  template <typename Key>
  static bool exist(const std::vector<Key>& vector, const Key& key)
  {
    for (size_t i = 0; i < vector.size(); i++) {
      if (vector[i] == key) {
        return true;
      }
    }
    return false;
  }

  template <typename Key, typename Compare = std::less<Key>>
  static bool exist(const std::set<Key, Compare>& set, const Key& key)
  {
    return (set.find(key) != set.end());
  }

  template <typename Key, typename Hash = std::hash<Key>>
  static bool exist(const std::unordered_set<Key, Hash>& set, const Key& key)
  {
    return (set.find(key) != set.end());
  }

  template <typename Key, typename Value, typename Compare = std::less<Key>>
  static bool exist(const std::map<Key, Value, Compare>& map, const Key& key)
  {
    return (map.find(key) != map.end());
  }

  template <typename Key, typename Value, typename Hash = std::hash<Key>>
  static bool exist(const std::unordered_map<Key, Value, Hash>& map, const Key& key)
  {
    return (map.find(key) != map.end());
  }

  template <typename T = nlohmann::json>
  static T getData(nlohmann::json value, std::vector<std::string> flag_list)
  {
    if (flag_list.empty()) {
      DRCLOG.error(Loc::current(), "The flag list is empty!");
    }
    for (size_t i = 0; i < flag_list.size(); i++) {
      value = value[flag_list[i]];
    }
    if (!value.is_null()) {
      return value;
    }
    std::string key;
    for (size_t i = 0; i < flag_list.size(); i++) {
      key += flag_list[i] + ".";
    }
    DRCLOG.error(Loc::current(), "The configuration file key '", key, "' does not exist!");
    return value;
  }

  template <typename T = nlohmann::json>
  static T getData(nlohmann::json value, std::string flag)
  {
    if (flag.empty()) {
      DRCLOG.error(Loc::current(), "The flag is empty!");
    }
    value = value[flag];
    if (!value.is_null()) {
      return value;
    }
    DRCLOG.error(Loc::current(), "The configuration file key '", flag, "' does not exist!");
    return value;
  }

  /**
   * @description: sigmoid
   * ---------------------
   * │ accuracy │ value  │
   * │  0.9999  │ 9.2102 │
   * │  0.999   │ 6.9068 │
   * │  0.99    │ 4.5951 │
   * │  0.9     │ 2.1972 │
   * ---------------------
   *
   * return 1.0 / { 1 + e ^ [ accuracy * (1 - 2 * value / threshold) ] }
   * notice : The closer the <value> is to the <threshold>, the closer the return value is to 1
   *
   */
  static double sigmoid(double value, double threshold);

  template <typename T, typename U>
  static double getRatio(T a, U b)
  {
    return (b > 0 ? static_cast<double>(a) / static_cast<double>(b) : 0.0);
  }

  template <typename T, typename U>
  static std::string getPercentage(T a, U b)
  {
    return getString(formatByTwoDecimalPlaces(getRatio(a, b) * 100), "%");
  }

  static std::ifstream* getInputFileStream(std::string file_path);

  static std::ofstream* getOutputFileStream(std::string file_path);

  template <typename T>
  static T* getFileStream(std::string file_path)
  {
    T* file = new T(file_path);
    if (!file->is_open()) {
      DRCLOG.error(Loc::current(), "Failed to open file '", file_path, "'!");
    }
    return file;
  }

  template <typename T>
  static void closeFileStream(T* t)
  {
    if (t != nullptr) {
      t->close();
      delete t;
    }
  }

  template <typename T, typename... Args>
  static std::string getString(T value, Args... args)
  {
    std::stringstream oss;
    pushStream(oss, value, args...);
    std::string string = oss.str();
    oss.clear();
    return string;
  }

  static std::string getBooleanName(bool value);

  template <typename T>
  static std::string getStringList(const std::vector<T>& value_list, const std::string& delimiter = ", ", const std::string& prefix = "[",
                                   const std::string& suffix = "]")
  {
    std::stringstream oss;
    oss << prefix;
    for (size_t i = 0; i < value_list.size(); ++i) {
      if (i > 0) {
        oss << delimiter;
      }
      oss << value_list[i];
    }
    oss << suffix;
    return oss.str();
  }

  template <typename Stream, typename T, typename... Args>
  static void pushStream(Stream* stream, T t, Args... args)
  {
    pushStream(*stream, t, args...);
  }

  template <typename Stream, typename T, typename... Args>
  static void pushStream(Stream& stream, T t, Args... args)
  {
    stream << t;
    pushStream(stream, args...);
  }

  template <typename Stream, typename T>
  static void pushStream(Stream& stream, T t)
  {
    stream << t;
  }

  static std::string escapeBackslash(std::string a);

  static bool isInteger(double a);

  static void checkFile(std::string file_path);

  static void createDirByFile(std::string file_path);

  static void createDir(std::string dir_path);

  static bool existFile(const std::string& file_path);

  static void changePermissions(const std::string& dir_path, std::filesystem::perms permissions);

  static void removeDir(const std::string& dir_path);

  static std::string getFileName(std::string file_path);

  static std::string getSpaceByTabNum(int32_t tab_num);

  static std::string getHex(int32_t number);

  static std::vector<std::string> splitString(std::string a, char tok);

  std::string getCompressedBase62(uint64_t origin);

  uint64_t getDecompressedBase62(std::string origin);

  std::string getCompressedBase128(uint64_t origin);

  uint64_t getDecompressedBase128(std::string origin);

  static std::string getTimestamp();

  static std::string formatSec(double sec);

  static std::string formatByTwoDecimalPlaces(double digit);

  template <typename T>
  static std::set<T> getDifference(std::set<T>& master, std::set<T>& set)
  {
    std::vector<T> master_list(master.begin(), master.end());
    std::vector<T> set_list(set.begin(), set.end());

    std::vector<T> result;
    std::set_difference(master_list.begin(), master_list.end(), set_list.begin(), set_list.end(), std::back_inserter(result));

    return std::set<T>(result.begin(), result.end());
  }

  /**
   * 从多个list中,每个选择一个元素并生成所有可能的组合,非递归
   */
  template <typename T>
  static std::vector<std::vector<T>> getCombList(std::vector<std::vector<T>>& list_list)
  {
    if (list_list.empty()) {
      return {};
    }
    std::vector<std::vector<T>> comb_list = {{}};
    for (const std::vector<T>& list : list_list) {
      std::vector<std::vector<T>> new_comb_list;
      for (const std::vector<T>& comb : comb_list) {
        for (const T& item : list) {
          std::vector<T> new_comb = comb;
          new_comb.push_back(item);
          new_comb_list.push_back(new_comb);
        }
      }
      comb_list = new_comb_list;
    }
    return comb_list;
  }

  /**
   * 从一个list中,生成只包含n个元素所有可能的组合,非递归
   */
  template <typename T>
  static std::vector<std::vector<T>> getCombList(std::vector<T>& list, int32_t n)
  {
    int32_t list_num = static_cast<int32_t>(list.size());
    if (n <= 0 || list_num < n) {
      return {};
    }
    std::vector<std::vector<T>> comb_list;
    for (int32_t mask = 0; mask < (1 << list_num); ++mask) {
      int32_t count = 0;
      for (int32_t i = 0; i < list_num; ++i) {
        if (mask & (1 << i)) {
          count++;
        }
      }
      if (count != n) {
        continue;
      }
      std::vector<T> curr_comb;
      for (int32_t i = 0; i < list_num; ++i) {
        if (mask & (1 << i)) {
          curr_comb.push_back(list[i]);
        }
      }
      comb_list.push_back(curr_comb);
    }
    return comb_list;
  }

#endif

 private:
  // self
  static Utility* _util_instance;

  Utility() = default;
  Utility(const Utility& other) = delete;
  Utility(Utility&& other) = delete;
  ~Utility() = default;
  Utility& operator=(const Utility& other) = delete;
  Utility& operator=(Utility&& other) = delete;
  // function
};

}  // namespace idrc

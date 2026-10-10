#include "GeometryThumbnail.h"

#include "GeometryPayload.h"
#include "GeometryTypes.h"
#include "ShapeRecord.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ecc::geometry {
namespace {

struct LayerStyle
{
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t alpha = 150;
};

std::optional<int> level_after_token(const std::string& upper_name, const std::string& token)
{
  size_t pos = upper_name.find(token);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos += token.size();
  if (pos >= upper_name.size() || !std::isdigit(static_cast<unsigned char>(upper_name[pos]))) {
    return std::nullopt;
  }
  int level = 0;
  while (pos < upper_name.size() && std::isdigit(static_cast<unsigned char>(upper_name[pos]))) {
    level = level * 10 + (upper_name[pos] - '0');
    ++pos;
  }
  return level;
}

std::optional<int> layer_level(const std::string& name)
{
  std::string upper = name;
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
  if (std::optional<int> level = level_after_token(upper, "METAL")) {
    return level;
  }
  if (std::optional<int> level = level_after_token(upper, "MET")) {
    return level;
  }
  if (std::optional<int> level = level_after_token(upper, "VIA")) {
    return level;
  }
  if (std::optional<int> level = level_after_token(upper, "M")) {
    return level;
  }
  return level_after_token(upper, "V");
}

LayerStyle hsl_fallback_style(size_t index)
{
  const double hue = static_cast<double>((index * 47) % 360);
  const double saturation = 0.6;
  const double lightness = 0.55;
  const double chroma = (1.0 - std::fabs(2.0 * lightness - 1.0)) * saturation;
  const double hue_prime = hue / 60.0;
  const double x = chroma * (1.0 - std::fabs(std::fmod(hue_prime, 2.0) - 1.0));
  double r1 = 0.0;
  double g1 = 0.0;
  double b1 = 0.0;
  if (hue_prime < 1.0) {
    r1 = chroma;
    g1 = x;
  } else if (hue_prime < 2.0) {
    r1 = x;
    g1 = chroma;
  } else if (hue_prime < 3.0) {
    g1 = chroma;
    b1 = x;
  } else if (hue_prime < 4.0) {
    g1 = x;
    b1 = chroma;
  } else if (hue_prime < 5.0) {
    r1 = x;
    b1 = chroma;
  } else {
    r1 = chroma;
    b1 = x;
  }
  const double match = lightness - chroma / 2.0;
  LayerStyle style;
  style.r = static_cast<uint8_t>(std::clamp((r1 + match) * 255.0, 0.0, 255.0));
  style.g = static_cast<uint8_t>(std::clamp((g1 + match) * 255.0, 0.0, 255.0));
  style.b = static_cast<uint8_t>(std::clamp((b1 + match) * 255.0, 0.0, 255.0));
  return style;
}

LayerStyle layer_style(const GeometryLayerMetadata& layer, size_t fallback_index)
{
  static constexpr std::array<std::array<uint8_t, 3>, 7> kMetalColors = {
      std::array<uint8_t, 3>{65, 130, 240},  std::array<uint8_t, 3>{240, 140, 40}, std::array<uint8_t, 3>{50, 180, 80},
      std::array<uint8_t, 3>{235, 205, 50},  std::array<uint8_t, 3>{195, 70, 215}, std::array<uint8_t, 3>{40, 200, 220},
      std::array<uint8_t, 3>{215, 80, 110},
  };
  static constexpr std::array<uint8_t, 3> kViaColor = {197, 200, 204};

  const std::optional<int> level = layer_level(layer.name);
  std::string upper = layer.name;
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
  std::string type = layer.type;
  std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
  const bool via_like = upper.find('V') != std::string::npos || type.find("via") != std::string::npos
                        || type.find("cut") != std::string::npos;
  const bool routing = type.find("routing") != std::string::npos;
  LayerStyle style;
  if (level && !via_like) {
    const auto& rgb = kMetalColors[(static_cast<size_t>(*level) - 1) % kMetalColors.size()];
    style.r = rgb[0];
    style.g = rgb[1];
    style.b = rgb[2];
    style.alpha = routing ? 200 : 130;
  } else if (via_like) {
    style.r = kViaColor[0];
    style.g = kViaColor[1];
    style.b = kViaColor[2];
    style.alpha = 160;
  } else {
    style = hsl_fallback_style(fallback_index);
  }
  return style;
}

template <typename T>
const T* payload_at(std::span<const std::byte> payloads, const ShapeRecord& record)
{
  if (record.payload_size < sizeof(T) || record.payload_offset + sizeof(T) > payloads.size_bytes()) {
    return nullptr;
  }
  return reinterpret_cast<const T*>(payloads.data() + record.payload_offset);
}

class ThumbnailCanvas
{
 public:
  ThumbnailCanvas(uint32_t width, uint32_t height, Rect32 world_rect)
      : _width(width), _height(height), _pixels(static_cast<size_t>(width) * height * 4, 255)
  {
    const double margin_x = 0.05 * (world_rect.hx - world_rect.lx);
    const double margin_y = 0.05 * (world_rect.hy - world_rect.ly);
    _world_lx = world_rect.lx - margin_x;
    _world_ly_max = world_rect.hy + margin_y;
    const double world_w = (world_rect.hx - world_rect.lx) + 2.0 * margin_x;
    const double world_h = (world_rect.hy - world_rect.ly) + 2.0 * margin_y;
    _scale = std::min(_width / world_w, _height / world_h);
    _offset_x = (_width - world_w * _scale) / 2.0;
    _offset_y = (_height - world_h * _scale) / 2.0;
  }

  void fill_rect(Rect32 rect, const LayerStyle& style)
  {
    rect = normalize(rect);
    int x0 = clamp_pixel_x(std::floor(pixel_x(rect.lx)));
    int x1 = clamp_pixel_x(std::floor(pixel_x(rect.hx)));
    int y0 = clamp_pixel_y(std::floor(pixel_y(rect.hy)));
    int y1 = clamp_pixel_y(std::floor(pixel_y(rect.ly)));
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        blend_pixel(x, y, style);
      }
    }
  }

  void draw_line(const LinePayload& line, const LayerStyle& style)
  {
    if (line.begin.y == line.end.y) {
      fill_rect(Rect32{std::min(line.begin.x, line.end.x), line.begin.y - line.width / 2, std::max(line.begin.x, line.end.x),
                       line.begin.y + line.width / 2},
                style);
    } else if (line.begin.x == line.end.x) {
      fill_rect(Rect32{line.begin.x - line.width / 2, std::min(line.begin.y, line.end.y), line.begin.x + line.width / 2,
                       std::max(line.begin.y, line.end.y)},
                style);
    } else {
      fill_rect(Rect32{std::min(line.begin.x, line.end.x) - line.width / 2, std::min(line.begin.y, line.end.y) - line.width / 2,
                       std::max(line.begin.x, line.end.x) + line.width / 2,
                       std::max(line.begin.y, line.end.y) + line.width / 2},
                style);
    }
  }

  void draw_point(const PointPayload& point, const LayerStyle& style)
  {
    const int cx = clamp_pixel_x(std::floor(pixel_x(point.point.x)));
    const int cy = clamp_pixel_y(std::floor(pixel_y(point.point.y)));
    for (int y = cy - 1; y <= cy + 1; ++y) {
      for (int x = cx - 1; x <= cx + 1; ++x) {
        if (x >= 0 && x < static_cast<int>(_width) && y >= 0 && y < static_cast<int>(_height)) {
          blend_pixel(x, y, style);
        }
      }
    }
  }

  const std::vector<uint8_t>& pixels() const { return _pixels; }

 private:
  double pixel_x(int32_t world_x) const { return _offset_x + (world_x - _world_lx) * _scale; }
  double pixel_y(int32_t world_y) const { return _offset_y + (_world_ly_max - world_y) * _scale; }

  int clamp_pixel_x(double value) const
  {
    return static_cast<int>(std::clamp(value, 0.0, static_cast<double>(_width) - 1.0));
  }
  int clamp_pixel_y(double value) const
  {
    return static_cast<int>(std::clamp(value, 0.0, static_cast<double>(_height) - 1.0));
  }

  void blend_pixel(int x, int y, const LayerStyle& style)
  {
    uint8_t* pixel = _pixels.data() + (static_cast<size_t>(y) * _width + x) * 4;
    const unsigned alpha = style.alpha;
    const unsigned inverse = 255 - alpha;
    pixel[0] = static_cast<uint8_t>((style.r * alpha + pixel[0] * inverse) / 255);
    pixel[1] = static_cast<uint8_t>((style.g * alpha + pixel[1] * inverse) / 255);
    pixel[2] = static_cast<uint8_t>((style.b * alpha + pixel[2] * inverse) / 255);
    pixel[3] = 255;
  }

  uint32_t _width;
  uint32_t _height;
  double _world_lx = 0.0;
  double _world_ly_max = 0.0;
  double _scale = 0.0;
  double _offset_x = 0.0;
  double _offset_y = 0.0;
  std::vector<uint8_t> _pixels;
};

void append_be32(std::vector<uint8_t>& out, uint32_t value)
{
  out.push_back(static_cast<uint8_t>(value >> 24));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value));
}

bool write_png_chunk(std::ofstream& out, const char type[4], const std::vector<uint8_t>& data)
{
  std::vector<uint8_t> length;
  append_be32(length, static_cast<uint32_t>(data.size()));
  out.write(reinterpret_cast<const char*>(length.data()), length.size());
  out.write(type, 4);
  if (!data.empty()) {
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
  }
  uLong crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
  if (!data.empty()) {
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()), data.size());
  }
  std::vector<uint8_t> crc_bytes;
  append_be32(crc_bytes, static_cast<uint32_t>(crc));
  out.write(reinterpret_cast<const char*>(crc_bytes.data()), crc_bytes.size());
  return static_cast<bool>(out);
}

bool write_png(const std::filesystem::path& png_path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba)
{
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 3));
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    const size_t row_begin = static_cast<size_t>(y) * width * 4;
    for (uint32_t x = 0; x < width; ++x) {
      const size_t pixel = row_begin + static_cast<size_t>(x) * 4;
      raw.push_back(rgba[pixel]);
      raw.push_back(rgba[pixel + 1]);
      raw.push_back(rgba[pixel + 2]);
    }
  }

  uLongf compressed_size = compressBound(raw.size());
  std::vector<uint8_t> compressed(compressed_size);
  if (compress2(compressed.data(), &compressed_size, raw.data(), raw.size(), Z_DEFAULT_COMPRESSION) != Z_OK) {
    return false;
  }
  compressed.resize(compressed_size);

  std::ofstream out(png_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  static constexpr std::array<uint8_t, 8> kSignature = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  out.write(reinterpret_cast<const char*>(kSignature.data()), kSignature.size());

  std::vector<uint8_t> ihdr;
  append_be32(ihdr, width);
  append_be32(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  if (!write_png_chunk(out, "IHDR", ihdr)) {
    return false;
  }
  if (!write_png_chunk(out, "IDAT", compressed)) {
    return false;
  }
  if (!write_png_chunk(out, "IEND", {})) {
    return false;
  }
  out.close();
  return static_cast<bool>(out);
}

}  // namespace

bool render_thumbnail_png(const GeometryStore& store, std::span<const GeometryLayerMetadata> layers,
                          const std::filesystem::path& png_path, uint32_t width, uint32_t height)
{
  if (width == 0 || height == 0 || png_path.empty()) {
    return false;
  }

  const std::span<const ShapeRecord> records = store.records();
  const std::span<const std::byte> payloads = store.payloads();

  bool has_alive = false;
  Rect32 bounds{std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max(),
                std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min()};
  for (const ShapeRecord& record : records) {
    if (record.state != ShapeState::kAlive) {
      continue;
    }
    const Rect32 bbox = normalize(record.bbox);
    bounds.lx = std::min(bounds.lx, bbox.lx);
    bounds.ly = std::min(bounds.ly, bbox.ly);
    bounds.hx = std::max(bounds.hx, bbox.hx);
    bounds.hy = std::max(bounds.hy, bbox.hy);
    has_alive = true;
  }
  if (!has_alive) {
    return false;
  }

  try {
    std::unordered_map<LayerId, size_t> layer_order;
    std::unordered_map<LayerId, LayerStyle> layer_styles;
    for (size_t i = 0; i < layers.size(); ++i) {
      const GeometryLayerMetadata& layer = layers[i];
      if (!layer_order.contains(layer.layer_id)) {
        layer_order.emplace(layer.layer_id, i);
        layer_styles.emplace(layer.layer_id, layer_style(layer, i));
      }
    }

    std::vector<const ShapeRecord*> alive_records;
    alive_records.reserve(records.size());
    for (const ShapeRecord& record : records) {
      if (record.state == ShapeState::kAlive) {
        alive_records.push_back(&record);
      }
    }
    std::stable_sort(alive_records.begin(), alive_records.end(), [&layer_order](const ShapeRecord* lhs, const ShapeRecord* rhs) {
      const bool lhs_known = layer_order.contains(lhs->layer_id);
      const bool rhs_known = layer_order.contains(rhs->layer_id);
      if (lhs_known != rhs_known) {
        return rhs_known;
      }
      if (!lhs_known) {
        return lhs->layer_id < rhs->layer_id;
      }
      return layer_order[lhs->layer_id] < layer_order[rhs->layer_id];
    });

    ThumbnailCanvas canvas(width, height, bounds);
    for (const ShapeRecord* record : alive_records) {
      LayerStyle style;
      const auto style_iter = layer_styles.find(record->layer_id);
      if (style_iter != layer_styles.end()) {
        style = style_iter->second;
      } else {
        // Layers without metadata carry structural background (die/core/row/instance bbox):
        // draw a neutral tint underneath every real layer.
        style = LayerStyle{170, 170, 170, 120};
      }
      switch (record->kind) {
        case ShapeKind::kRect: {
          const RectPayload* payload = payload_at<RectPayload>(payloads, *record);
          canvas.fill_rect(payload != nullptr ? payload->rect : record->bbox, style);
          break;
        }
        case ShapeKind::kLine: {
          const LinePayload* payload = payload_at<LinePayload>(payloads, *record);
          if (payload != nullptr) {
            canvas.draw_line(*payload, style);
          }
          break;
        }
        case ShapeKind::kPoint: {
          const PointPayload* payload = payload_at<PointPayload>(payloads, *record);
          if (payload != nullptr) {
            canvas.draw_point(*payload, style);
          }
          break;
        }
        default:
          break;
      }
    }

    return write_png(png_path, width, height, canvas.pixels());
  } catch (...) {
    return false;
  }
}

}  // namespace ecc::geometry

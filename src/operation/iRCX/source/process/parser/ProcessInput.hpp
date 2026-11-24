#pragma once

#include <array>
#include <istream>
#include <string>

namespace ircx::process_detail {
// The readers own streams with the default exception mask. istream::read turns
// filebuf I/O failures into stream state; iterating filebuf directly does not.
inline bool readText(std::istream& stream, std::string& text)
{
  text.clear();
  std::array<char, 8192> buffer;
  while (stream) {
    stream.read(buffer.data(), buffer.size());
    text.append(buffer.data(), static_cast<std::size_t>(stream.gcount()));
  }
  if (stream.bad() || !stream.eof()) {
    text.clear();
    return false;
  }
  return true;
}
}  // namespace ircx::process_detail

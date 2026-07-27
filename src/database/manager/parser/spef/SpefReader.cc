#include "SpefParser.hh"

#include <cstdio>
#include <memory>

#include "SpefAnnotationScanner.hh"
#include "log/Log.hh"

int spef_parse(spef::ParserContext* context);
void spef_restart(FILE* input_file);
extern FILE* spef_in;

namespace spef {

Exchange* parseSpefFile(const char* spef_path)
{
  if (spef_path == nullptr) {
    return nullptr;
  }

  FILE* file = std::fopen(spef_path, "r");
  if (file == nullptr) {
    LOG_ERROR << "open spef file failed: " << spef_path;
    return nullptr;
  }

  ParserContext context(spef_path);
  spef_in = file;
  spef_restart(file);
  const int parse_status = spef_parse(&context);
  std::fclose(file);
  spef_in = nullptr;

  if (parse_status != 0 || !context.ok()) {
    LOG_ERROR << "parse spef file failed: " << spef_path << " " << context.errorMessage();
    return nullptr;
  }

  context.finishNet();
  auto exchange = std::make_unique<Exchange>(std::move(context.exchange()));
  augmentAnnotations(*exchange);
  return exchange.release();
}

bool SpefReader::read(const std::string& file_path)
{
  spef_file_.reset(parseSpefFile(file_path.c_str()));
  return spef_file_ != nullptr;
}

void SpefReader::expandName()
{
  if (spef_file_ != nullptr) {
    expandAllNames(*spef_file_);
  }
}

std::string SpefReader::getSpefCapUnit() const
{
  return spef_file_ == nullptr ? std::string{} : spef::getSpefCapUnit(*spef_file_);
}

std::string SpefReader::getSpefResUnit() const
{
  return spef_file_ == nullptr ? std::string{} : spef::getSpefResUnit(*spef_file_);
}

}  // namespace spef

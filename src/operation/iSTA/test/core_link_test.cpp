// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>

#include "DataManager.hpp"
#include "Logger.hpp"

int main()
{
  ista::Logger::initInst();
  ista::DataManager::initInst();
  auto& database = ista::DataManager::getInst().getDatabase();
  database.network().setName("core");
  database.network().pins()["in"].set_is_port(true);
  if (database.network().name() != "core" || database.network().pins().size() != 1) {
    throw std::runtime_error("independently linked data core lost its database");
  }
  ista::DataManager::destroyInst();
  ista::Logger::destroyInst();
}

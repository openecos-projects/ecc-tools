// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace irt {

class Config;
class Database;
class DataManager;

// A source projects into the router's existing work model and publishes its
// results. Its database must outlive the attached routing session. Attach or
// detach only outside init/run/destroy (including abort) of that session.
class RTDesignSource
{
 public:
  virtual ~RTDesignSource() = default;
  virtual void wrapDatabase(Database& database, const Config& config) = 0;
  virtual void outputTrackGrid(Database& database) = 0;
  virtual void outputGCellGrid(Database& database) = 0;
  virtual void outputNetList(DataManager& results) = 0;
};

}  // namespace irt

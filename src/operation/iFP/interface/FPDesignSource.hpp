// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace ifp {

class Database;

// Projects an external design database into iFP's existing work model and
// publishes the completed result. The source and its backing database must
// outlive an active iFP session.
class FPDesignSource
{
 public:
  virtual ~FPDesignSource() = default;
  virtual void wrapDatabase(Database& database) = 0;
  virtual void output(Database& database) = 0;
};

}  // namespace ifp

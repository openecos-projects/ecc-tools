// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace eccdb {
class PhysicalDesignService;
}
namespace utl {
class Logger;
}
namespace drt {
class frDesign;
struct RouterConfiguration;

namespace io {

// These imports create DRT-owned objects directly from EccDB. They do not
// create an OpenDB database or retain pointers to OpenDB database objects.
void readEccdbTechnology(const eccdb::PhysicalDesignService& physical,
                         frDesign& design,
                         RouterConfiguration& configuration,
                         utl::Logger& logger);
void readEccdbLibrary(const eccdb::PhysicalDesignService& physical,
                      frDesign& design,
                      utl::Logger& logger);
void readEccdbDesign(const eccdb::PhysicalDesignService& physical,
                     frDesign& design,
                     RouterConfiguration& configuration,
                     utl::Logger& logger);

}  // namespace io
}  // namespace drt

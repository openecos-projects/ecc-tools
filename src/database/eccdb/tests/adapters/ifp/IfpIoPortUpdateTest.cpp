// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

#include "DataManager.hpp"
#include "FPInterface.hpp"
#include "IOPin.hpp"
#include "IOPort.hpp"
#include "IdbDesign.h"
#include "IdbLayer.h"
#include "def_service.h"
#include "idm.h"
#include "lef_service.h"

namespace {

class IfpLegacyIoPortUpdateTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    ifp::DataManager::initInst();

    lef_service_ = std::make_unique<idb::IdbLefService>();
    layout_ = lef_service_->get_layout();
    def_service_ = std::make_unique<idb::IdbDefService>(layout_);
    dmInst->set_idb_lef_service(lef_service_.get());
    dmInst->set_idb_def_service(def_service_.get());

    layer_ = layout_->get_layers()->set_layer("M1", "ROUTING");
    ASSERT_NE(layer_, nullptr);
    design_ = def_service_->get_design();
    ASSERT_NE(design_, nullptr);
  }

  void TearDown() override
  {
    ifp::DataManager::destroyInst();
    ifp::FPInterface::destroyInst();

    // The process-wide iDB manager does not own these test-local services.
    dmInst->set_idb_def_service(nullptr);
    dmInst->set_idb_lef_service(nullptr);
    def_service_.reset();
    lef_service_.reset();
  }

  idb::IdbPin* addIoPin(const std::string& name, const std::string& net_name)
  {
    idb::IdbPin* pin = design_->createOrFindIoPin(name);
    if (pin == nullptr) {
      return nullptr;
    }
    idb::IdbTerm* term = pin->set_term();
    term->set_name(name);
    idb::IdbNet* net = design_->get_net_list()->add_net(net_name, idb::IdbConnectType::kSignal);
    if (net == nullptr || !design_->connectPinToNet(pin, net)) {
      return nullptr;
    }
    return pin;
  }

  idb::IdbPort* addPort(idb::IdbPin& pin, bool explicit_port, int32_t origin_x, int32_t origin_y, idb::IdbOrient orient,
                        int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y)
  {
    idb::IdbTerm* term = pin.get_term();
    term->set_has_port(explicit_port);
    term->set_placement_status_place();
    pin.set_location(origin_x, origin_y);
    pin.set_orient(orient);

    idb::IdbPort* port = term->add_port();
    port->set_orient(orient);
    port->set_placement_status_place();
    idb::IdbLayerShape* shape = port->add_layer_shape();
    shape->set_type_rect();
    shape->set_layer(layer_);
    shape->add_rect(ll_x, ll_y, ur_x, ur_y);
    port->set_coordinate(origin_x, origin_y);

    // Build the same parser-populated cache used by an imported fixture. The
    // tests below intentionally validate PORT records, not this legacy cache.
    term->set_bounding_box(ll_x, ll_y, ur_x, ur_y);
    pin.set_bounding_box();
    return port;
  }

  ifp::IOPin& addUpdate(const std::string& name)
  {
    auto& updates = FPDM.getDatabase().get_io_pin_list();
    updates.emplace_back();
    updates.back().set_name(name);
    return updates.back();
  }

  static ifp::IOPort newPort(int32_t x, int32_t y, int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y)
  {
    ifp::IOPort port;
    port.set_layer_name("M1");
    port.set_coord(x, y);
    port.set_rect(ll_x, ll_y, ur_x, ur_y);
    port.set_placed(true);
    return port;
  }

  static void expectRect(const idb::IdbRect& rectangle, int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y)
  {
    EXPECT_EQ(rectangle.get_low_x(), ll_x);
    EXPECT_EQ(rectangle.get_low_y(), ll_y);
    EXPECT_EQ(rectangle.get_high_x(), ur_x);
    EXPECT_EQ(rectangle.get_high_y(), ur_y);
  }

  std::unique_ptr<idb::IdbLefService> lef_service_;
  std::unique_ptr<idb::IdbDefService> def_service_;
  idb::IdbLayout* layout_ = nullptr;
  idb::IdbDesign* design_ = nullptr;
  idb::IdbLayer* layer_ = nullptr;
};

TEST_F(IfpLegacyIoPortUpdateTest, UpdatedPortListReplacesPortsConsumesDeltaAndEmptyListClearsOnlyPhysicalPorts)
{
  idb::IdbPin* pin = addIoPin("replace_pin", "replace_net");
  ASSERT_NE(pin, nullptr);
  static_cast<void>(addPort(*pin, true, 10, 20, idb::IdbOrient::kN_R0, -5, -2, 5, 2));
  idb::IdbTerm* term = pin->get_term();
  idb::IdbNet* net = pin->get_net();
  ASSERT_NE(net, nullptr);
  ASSERT_EQ(term->get_port_list().size(), 1u);

  auto& update = addUpdate("replace_pin");
  update.set_port_exist_updated(true);
  update.set_port_exist(true);
  update.set_direct_location(true);
  update.set_coord(100, 200);
  update.set_orient(ifp::PlacementOrientation::kN);
  update.set_placed(true);
  update.get_new_port_list().push_back(newPort(100, 200, -8, -3, 8, 3));
  update.get_new_port_list().push_back(newPort(140, 240, -2, -6, 2, 6));
  update.set_updated(true);

  ASSERT_NO_THROW(FPI.outputIOPinList());

  // upstream iFP replaces only physical PORTs. The Term, Net, and their
  // relationship remain valid while the full new list is installed.
  EXPECT_EQ(pin->get_term(), term);
  EXPECT_EQ(pin->get_net(), net);
  EXPECT_EQ(net->get_io_pins()->find_pin("replace_pin"), pin);
  ASSERT_TRUE(term->is_port_exist());
  ASSERT_EQ(term->get_port_list().size(), 2u);
  auto* first = term->get_port_list()[0];
  auto* second = term->get_port_list()[1];
  EXPECT_EQ(first->get_coordinate()->get_x(), 100);
  EXPECT_EQ(first->get_coordinate()->get_y(), 200);
  EXPECT_EQ(first->get_placement_status(), idb::IdbPlacementStatus::kPlaced);
  ASSERT_EQ(first->get_layer_shape().size(), 1u);
  ASSERT_EQ(first->get_layer_shape()[0]->get_rect_list().size(), 1u);
  expectRect(*first->get_layer_shape()[0]->get_rect_list()[0], -8, -3, 8, 3);
  EXPECT_EQ(second->get_coordinate()->get_x(), 140);
  EXPECT_EQ(second->get_coordinate()->get_y(), 240);
  EXPECT_EQ(second->get_placement_status(), idb::IdbPlacementStatus::kPlaced);
  ASSERT_EQ(second->get_layer_shape().size(), 1u);
  ASSERT_EQ(second->get_layer_shape()[0]->get_rect_list().size(), 1u);
  expectRect(*second->get_layer_shape()[0]->get_rect_list()[0], -2, -6, 2, 6);
  EXPECT_FALSE(update.is_updated());
  EXPECT_TRUE(update.get_new_port_list().empty());

  // A consumed update is inert on a subsequent writer call.
  ASSERT_NO_THROW(FPI.outputIOPinList());
  EXPECT_EQ(term->get_port_list().size(), 2u);

  // In the upstream contract, an updated empty new-port list clears the
  // physical PORTs but never destroys the logical terminal or its Net link.
  update.set_updated(true);
  ASSERT_NO_THROW(FPI.outputIOPinList());
  EXPECT_EQ(pin->get_term(), term);
  EXPECT_EQ(pin->get_net(), net);
  EXPECT_EQ(net->get_io_pins()->find_pin("replace_pin"), pin);
  EXPECT_TRUE(term->get_port_list().empty());
  EXPECT_FALSE(update.is_updated());
  EXPECT_TRUE(update.get_new_port_list().empty());
}

TEST_F(IfpLegacyIoPortUpdateTest, UpdatedPortListReplacesExplicitAndImplicitTermPorts)
{
  idb::IdbPin* explicit_pin = addIoPin("explicit_pin", "explicit_net");
  idb::IdbPin* implicit_pin = addIoPin("implicit_pin", "implicit_net");
  ASSERT_NE(explicit_pin, nullptr);
  ASSERT_NE(implicit_pin, nullptr);
  static_cast<void>(addPort(*explicit_pin, true, 11, 22, idb::IdbOrient::kN_R0, -7, -3, 7, 3));
  static_cast<void>(addPort(*implicit_pin, false, 15, 25, idb::IdbOrient::kN_R0, -4, -1, 4, 1));
  idb::IdbTerm* explicit_term = explicit_pin->get_term();
  idb::IdbTerm* implicit_term = implicit_pin->get_term();
  idb::IdbNet* explicit_net = explicit_pin->get_net();
  idb::IdbNet* implicit_net = implicit_pin->get_net();

  auto& explicit_update = addUpdate("explicit_pin");
  explicit_update.set_port_exist_updated(true);
  explicit_update.set_port_exist(true);
  explicit_update.set_direct_location(true);
  explicit_update.set_coord(100, 200);
  explicit_update.set_orient(ifp::PlacementOrientation::kN);
  explicit_update.set_placed(true);
  explicit_update.get_new_port_list().push_back(newPort(40, 50, -2, -2, 2, 2));
  explicit_update.set_updated(true);

  auto& implicit_update = addUpdate("implicit_pin");
  implicit_update.set_port_exist_updated(true);
  implicit_update.set_port_exist(false);
  implicit_update.set_direct_location(true);
  implicit_update.set_coord(500, 600);
  implicit_update.set_orient(ifp::PlacementOrientation::kN);
  implicit_update.set_placed(true);
  implicit_update.get_new_port_list().push_back(newPort(60, 70, -3, -2, 3, 2));
  implicit_update.set_updated(true);

  ASSERT_NO_THROW(FPI.outputIOPinList());

  ASSERT_EQ(explicit_term->get_port_list().size(), 1u);
  EXPECT_TRUE(explicit_term->is_port_exist());
  auto* explicit_port = explicit_term->get_port_list().front();
  EXPECT_EQ(explicit_port->get_coordinate()->get_x(), 40);
  EXPECT_EQ(explicit_port->get_coordinate()->get_y(), 50);
  EXPECT_EQ(explicit_port->get_placement_status(), idb::IdbPlacementStatus::kPlaced);
  expectRect(*explicit_port->get_layer_shape()[0]->get_rect_list()[0], -2, -2, 2, 2);
  EXPECT_EQ(explicit_pin->get_location()->get_x(), 100);
  EXPECT_EQ(explicit_pin->get_location()->get_y(), 200);
  EXPECT_EQ(explicit_pin->get_net(), explicit_net);
  EXPECT_EQ(explicit_net->get_io_pins()->find_pin("explicit_pin"), explicit_pin);

  ASSERT_EQ(implicit_term->get_port_list().size(), 1u);
  EXPECT_FALSE(implicit_term->is_port_exist());
  auto* implicit_port = implicit_term->get_port_list().front();
  EXPECT_EQ(implicit_port->get_coordinate()->get_x(), 60);
  EXPECT_EQ(implicit_port->get_coordinate()->get_y(), 70);
  EXPECT_EQ(implicit_port->get_placement_status(), idb::IdbPlacementStatus::kPlaced);
  expectRect(*implicit_port->get_layer_shape()[0]->get_rect_list()[0], -3, -2, 3, 2);
  EXPECT_EQ(implicit_pin->get_location()->get_x(), 500);
  EXPECT_EQ(implicit_pin->get_location()->get_y(), 600);
  EXPECT_EQ(implicit_pin->get_net(), implicit_net);
  EXPECT_EQ(implicit_net->get_io_pins()->find_pin("implicit_pin"), implicit_pin);

  const auto& updates = FPDM.getDatabase().get_io_pin_list();
  ASSERT_EQ(updates.size(), 2u);
  EXPECT_FALSE(updates[0].is_updated());
  EXPECT_TRUE(updates[0].get_new_port_list().empty());
  EXPECT_FALSE(updates[1].is_updated());
  EXPECT_TRUE(updates[1].get_new_port_list().empty());
}

TEST_F(IfpLegacyIoPortUpdateTest, UnmodifiedIoPinSkipsPendingPortList)
{
  idb::IdbPin* pin = addIoPin("unchanged_pin", "unchanged_net");
  ASSERT_NE(pin, nullptr);
  idb::IdbPort* existing = addPort(*pin, true, 10, 20, idb::IdbOrient::kN_R0, -5, -2, 5, 2);
  ASSERT_NE(existing, nullptr);
  idb::IdbTerm* term = pin->get_term();
  idb::IdbNet* net = pin->get_net();

  auto& pending = addUpdate("unchanged_pin");
  pending.get_new_port_list().push_back(newPort(40, 50, -3, -3, 3, 3));
  ASSERT_FALSE(pending.is_updated());

  ASSERT_NO_THROW(FPI.outputIOPinList());

  ASSERT_EQ(term->get_port_list().size(), 1u);
  EXPECT_EQ(term->get_port_list().front(), existing);
  expectRect(*existing->get_layer_shape()[0]->get_rect_list()[0], -5, -2, 5, 2);
  EXPECT_EQ(pin->get_net(), net);
  EXPECT_EQ(net->get_io_pins()->find_pin("unchanged_pin"), pin);
  EXPECT_FALSE(pending.is_updated());
  EXPECT_EQ(pending.get_new_port_list().size(), 1u);
}

}  // namespace

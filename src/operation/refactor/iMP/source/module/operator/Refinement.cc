#include "Refinement.hh"
#include <iostream>
#include <fstream>
#include <cassert>
#include <numeric>
#include <cmath>
#include <random>
#include <limits>

namespace imp {

Refinement::Refinement(std::weak_ptr<ParserEngine> parser)
    : _parser(parser), _root_block(nullptr), _macro_halo(0.0) {
}

Refinement::~Refinement() {
}

void Refinement::initPostProcessingData(
    float macro_halo_micron, 
    const std::string& original_pin_dir, 
    int exp_space_x, 
    int exp_space_y, 
    int search_space_x, 
    int search_space_y, 
    int gap, 
    int virtual_macro_size, 
    bool beikaobei, 
    float h_weight, 
    float v_weight, 
    bool consider_std
) {
    std::cout << "Initializing post-processing data..." << std::endl;

    auto shared_parser = _parser.lock();
    assert(shared_parser && "Parser has expired");

    idb::IdbBuilder* idb_builder = shared_parser->getIdbBuilder();
    dbu = idb_builder->get_def_service()->get_layout()->get_units()->get_micron_dbu();
    _macro_halo = dbu * macro_halo_micron;
    _original_pin_dir = original_pin_dir;
    _exp_space_x = dbu * exp_space_x;
    _exp_space_y = dbu * exp_space_y;
    _search_space_x = dbu * search_space_x;
    _search_space_y = dbu * search_space_y;
    _gap = dbu * gap;
    _virtual_macro_size = dbu * virtual_macro_size;
    _beikaobei = beikaobei;
    _h_weight = h_weight;
    _v_weight = v_weight;
    _consider_std = consider_std;

    std::cout << "Macro halo set to: " << _macro_halo << " in database units" << std::endl;

    extractCoreData();
    extractMacroData();
    extractBlockageData();
    extractPadData();
}

void Refinement::extractCoreData()
{
    std::cout << "Extracting core data..." << std::endl;

    idb::IdbBuilder* idb_builder = _parser.lock()->getIdbBuilder();
    auto* layout = idb_builder->get_def_service()->get_layout();
    idb::IdbCore* core = layout->get_core();

    auto* bounding_box = core->get_bounding_box();

    _core.lx = bounding_box->get_low_x();
    _core.ly = bounding_box->get_low_y();
    _core.ux = bounding_box->get_high_x();
    _core.uy = bounding_box->get_high_y();

    std::cout << "Extracted core bounds: (" << _core.lx << ", " << _core.ly << "), (" 
              << _core.ux << ", " << _core.uy << ")" << std::endl;
}

void Refinement::extractMacroData()
{
    const auto& instances = _parser.lock()->get_instances();
    for (const auto& [name, inst_ptr] : instances) {
        if (inst_ptr->get_cell_master().isMacro()) {
            MacroInfo macro_info;
            macro_info.id = inst_ptr->get_name();
            macro_info.name = inst_ptr->get_name();

            auto bbox = inst_ptr->boundingbox();
            macro_info.x = bbox.min_corner().x();
            macro_info.y = bbox.min_corner().y();
            macro_info.width = bbox.max_corner().x() - bbox.min_corner().x();
            macro_info.height = bbox.max_corner().y() - bbox.min_corner().y(); 
            macro_info.orient = orientToString(inst_ptr->get_orient());

            if (inst_ptr->isFixed()) {
                macro_info.is_fixed = true;
                _fix_macros.push_back(macro_info);
            } else {
                macro_info.is_fixed = false;
                _mov_macros.push_back(macro_info);
            }
        }
    }
}

void Refinement::extractBlockageData()
{
    std::cout << "Extracting blockage data..." << std::endl;

    idb::IdbBuilder* idb_builder = _parser.lock()->getIdbBuilder();
    idb::IdbBlockageList* blockage_list = idb_builder->get_def_service()->get_design()->get_blockage_list();

    if (blockage_list != nullptr) {
        for (auto* blockage : blockage_list->get_blockage_list()) {
            for (auto* rect : blockage->get_rect_list()) {
                BlockageInfo blockage_info;
                blockage_info.lx = rect->get_low_x();
                blockage_info.ly = rect->get_low_y();
                blockage_info.ux = rect->get_high_x();
                blockage_info.uy = rect->get_high_y();

                _blockages.push_back(blockage_info);
            }
        }
    }

    const auto& instances = _parser.lock()->get_instances();
    for (const auto& [name, inst_ptr] : instances) {
        if (inst_ptr->get_cell_master().isMacro() && inst_ptr->isFixed()) {
            BlockageInfo blockage_info;
            int lx = inst_ptr->get_lx(); 
            int ly = inst_ptr->get_ly(); 
            int width = inst_ptr->get_width(); 
            int height = inst_ptr->get_height();

            blockage_info.lx = lx;
            blockage_info.ly = ly;

            switch (inst_ptr->get_orient()) {
                case Orient::kN_R0:
                    blockage_info.ux = lx + width;
                    blockage_info.uy = ly + height;
                    break;
                case Orient::kW_R90:
                    blockage_info.ux = lx + height;
                    blockage_info.uy = ly + width;
                    break;
                case Orient::kS_R180:
                    blockage_info.ux = lx + width;
                    blockage_info.uy = ly + height;
                    break;
                case Orient::kE_R270:
                    blockage_info.ux = lx + height;
                    blockage_info.uy = ly + width;
                    break;
                case Orient::kFS_MX:
                    blockage_info.ux = lx + width;
                    blockage_info.uy = ly + height;
                    break;
                case Orient::kFN_MY:
                    blockage_info.ux = lx + width;
                    blockage_info.uy = ly + height;
                    break;
                default:
                    blockage_info.ux = lx + width;
                    blockage_info.uy = ly + height;
                    break;
            }
            _blockages.push_back(blockage_info);
        }
    }

    std::cout << "Extracted " << _blockages.size() << " blockages." << std::endl;
}

void Refinement::extractPadData()
{
    std::cout << "Extracting pad data..." << std::endl;

    const auto& instances = _parser.lock()->get_instances();
    for (const auto& [name, inst_ptr] : instances) {
        if (inst_ptr->get_cell_master().isIOCell()) {
            PadInfo pad_info;
            pad_info.id = inst_ptr->get_name();
            pad_info.name = inst_ptr->get_name();

            auto bbox = inst_ptr->boundingbox();
            pad_info.x = bbox.min_corner().x();
            pad_info.y = bbox.min_corner().y();

            _pads.push_back(pad_info);
        }
    }

    std::cout << "Extracted " << _pads.size() << " pads." << std::endl;
}

std::string Refinement::orientToString(Orient orient)
{
    switch (orient) {
        case Orient::kN_R0:
            return "R0";
        case Orient::kW_R90:
            return "R90";
        case Orient::kS_R180:
            return "R180";
        case Orient::kE_R270:
            return "R270";
        case Orient::kFN_MY:
            return "MY";
        case Orient::kFS_MX:
            return "MX";
        case Orient::kFW_MX90:
            return "MX90";
        case Orient::kFE_MY90:
            return "MY90";
        default:
            return "None";
    }
}

void Refinement::expandMacros() {
    _exp_mov_macros.clear();

    for (const auto& macro : _mov_macros) {
        MacroInfo expanded_macro = macro;
        
        int32_t original_lx = macro.x;
        int32_t original_ly = macro.y;
        int32_t original_ux = macro.x + macro.width;
        int32_t original_uy = macro.y + macro.height;

        int32_t expanded_lx = original_lx - static_cast<int32_t>(_macro_halo);
        int32_t expanded_ly = original_ly - static_cast<int32_t>(_macro_halo);
        int32_t expanded_ux = original_ux + static_cast<int32_t>(_macro_halo);
        int32_t expanded_uy = original_uy + static_cast<int32_t>(_macro_halo);

        expanded_ly -= _exp_space_y;
        expanded_uy += _exp_space_y;

        if (macro.orient == _original_pin_dir) {
            expanded_ux += _exp_space_x;
        } else if (macro.orient == "MX") {
            expanded_lx -= _exp_space_x;
        } else {
            std::cerr << "Warning: Macro " << macro.name << " has unsupported orientation: "
                      << macro.orient << ". No lateral expansion applied." << std::endl;
        }

        expanded_macro.x = expanded_lx;
        expanded_macro.y = expanded_ly;
        expanded_macro.width = expanded_ux - expanded_lx;
        expanded_macro.height = expanded_uy - expanded_ly;

        _exp_mov_macros.push_back(expanded_macro);
    }
}

void Refinement::restoreMacros() {
    if (_exp_mov_macros.size() != _mov_macros.size()) {
        std::cerr << "Error: The number of macros in _exp_mov_macros does not match _mov_macros." << std::endl;
        return;
    }

    for (size_t i = 0; i < _mov_macros.size(); ++i) {
        auto& exp_macro = _exp_mov_macros[i];
        auto& original_macro = _mov_macros[i];

        int32_t restored_ly = exp_macro.y + static_cast<int32_t>(_macro_halo + _exp_space_y);
        int32_t restored_uy = restored_ly + exp_macro.height - 2 * (static_cast<int32_t>(_macro_halo) + _exp_space_y);

        int32_t restored_lx = exp_macro.x + static_cast<int32_t>(_macro_halo);
        int32_t restored_ux = restored_lx + exp_macro.width - 2 * static_cast<int32_t>(_macro_halo);

        if (exp_macro.orient == _original_pin_dir) {
            restored_ux -= _exp_space_x;
        } else if (exp_macro.orient == "MX") {
            restored_lx += _exp_space_x;
        } else {
            std::cerr << "Warning: Macro " << exp_macro.name << " has unsupported orientation: "
                      << exp_macro.orient << ". No lateral contraction applied." << std::endl;
        }

        original_macro.x = restored_lx;
        original_macro.y = restored_ly;
        original_macro.orient = exp_macro.orient;
        original_macro.is_fixed = true;
    }

    std::cout << "Macros successfully restored to original dimensions." << std::endl;
}

std::vector<std::vector<Grid>> Refinement::divideCoreIntoGridsWithMacroGCD()
{
    std::vector<std::vector<Grid>> grids;

    _gcd_grid_width = _exp_mov_macros[0].width;
    _gcd_grid_height = _exp_mov_macros[0].height;

    int total_width = 0;
    int total_height = 0;
    for (const auto& macro : _exp_mov_macros) {
        total_width += macro.width;
        total_height += macro.height;
        _gcd_grid_width = std::gcd(_gcd_grid_width, macro.width);
        _gcd_grid_height = std::gcd(_gcd_grid_height, macro.height);
    }
    double average_macro_width = static_cast<double>(total_width) / _exp_mov_macros.size();
    double average_macro_height = static_cast<double>(total_height) / _exp_mov_macros.size();

    if (_gcd_grid_width > average_macro_width * 2 / 3) _gcd_grid_width /= 2;
    if (_gcd_grid_height > average_macro_height * 2 / 3) _gcd_grid_height /= 2;

    for (auto& macro : _exp_mov_macros) {
        macro.grid_count_x = macro.width / _gcd_grid_width;
        macro.grid_count_y = macro.height / _gcd_grid_height;

        // std::cout << "Macro " << macro.name << " occupies " << macro.grid_count_x << " grids horizontally and "
        //           << macro.grid_count_y << " grids vertically." << std::endl;
    }

    std::cout << "Grid width based on GCD of macro widths: " << _gcd_grid_width << std::endl;
    std::cout << "Grid height based on GCD of macro heights: " << _gcd_grid_height << std::endl;

    _gcd_num_grid_x = (_core.ux - _core.lx) / _gcd_grid_width;
    _gcd_num_grid_y = (_core.uy - _core.ly) / _gcd_grid_height;

    _gcd_reminder_x = (_core.ux - _core.lx) % _gcd_grid_width;
    _gcd_reminder_y = (_core.uy - _core.ly) % _gcd_grid_height;

    std::cout << "Number of grids: " << _gcd_num_grid_x << " x " << _gcd_num_grid_y << std::endl;

    grids.resize(_gcd_num_grid_x);
    for (int i = 0; i < _gcd_num_grid_x; ++i) {
        grids[i].resize(_gcd_num_grid_y);
    }

    int current_x = _core.lx;
    int current_y = _core.ly;

    for (int i = 0; i < _gcd_num_grid_x; ++i) {
        for (int j = 0; j < _gcd_num_grid_y; ++j) {
            Grid& grid = grids[i][j];
            // 如果处于前半部分，按照正常计算
            if (i < _gcd_num_grid_x / 2) {
                grid.x_start = current_x + i * _gcd_grid_width;
            } else {
                // 如果处于后半部分，额外考虑余量
                grid.x_start = current_x + i * _gcd_grid_width + _gcd_reminder_x;
            }

            if (j < _gcd_num_grid_y / 2) {
                grid.y_start = current_y + j * _gcd_grid_height;
            } else {
                grid.y_start = current_y + j * _gcd_grid_height + _gcd_reminder_y;
            }
            grid.is_used = false;
        }
    }

    for (const auto& blockage : _blockages) {
        for (int i = 0; i < _gcd_num_grid_x; ++i) {
            for (int j = 0; j < _gcd_num_grid_y; ++j) {
                Grid& grid = grids[i][j];

                int grid_x_end = grid.x_start + _gcd_grid_width;
                int grid_y_end = grid.y_start + _gcd_grid_height;

                bool overlaps = !(grid_x_end < blockage.lx || grid.x_start > blockage.ux ||
                                  grid_y_end < blockage.ly || grid.y_start > blockage.uy);

                if (overlaps) {
                    grid.is_used = true;
                }
            }
        }
    }

    assert(grids[0][0].x_start == _core.lx && grids[0][0].y_start == _core.ly);
    assert(grids[_gcd_num_grid_x - 1][_gcd_num_grid_y - 1].x_start + _gcd_grid_width == _core.ux &&
           grids[_gcd_num_grid_x - 1][_gcd_num_grid_y - 1].y_start + _gcd_grid_height == _core.uy);

    return grids;
}

void Refinement::adjustMacroOrientationBasedOnGridPosition() {
    int mid_grid_x = _gcd_num_grid_x / 2;

    for (auto& macro : _exp_mov_macros) {
        if (macro.grid_x >= mid_grid_x) {
            if (macro.orient != "MX") {
                std::cout << "Changing orientation of macro " << macro.name 
                          << " from " << macro.orient << " to MX." << std::endl;
                macro.orient = "MX";
            }
        } else {
            if (macro.orient != "R0") {
                std::cout << "Changing orientation of macro " << macro.name 
                          << " from " << macro.orient << " to R0." << std::endl;
                macro.orient = "R0";
            }
        }
    }
}

void Refinement::placeMacrosNearBoundaryOptimized() {

    std::vector<std::pair<size_t, double>> macro_indices_with_distances;

    for (size_t i = 0; i < _exp_mov_macros.size(); ++i) {
        const auto& macro = _exp_mov_macros[i];
        double distance_to_left = static_cast<double>(macro.x - _core.lx);
        double distance_to_right = static_cast<double>(_core.ux - (macro.x + macro.width));
        double distance_to_bottom = static_cast<double>(macro.y - _core.ly);
        double distance_to_top = static_cast<double>(_core.uy - (macro.y + macro.height));

        double min_distance = std::min({distance_to_left, distance_to_right, distance_to_bottom, distance_to_top});
        macro_indices_with_distances.emplace_back(i, min_distance);
    }

    std::sort(macro_indices_with_distances.begin(), macro_indices_with_distances.end(),
              [](const std::pair<size_t, double>& a, const std::pair<size_t, double>& b) {
                  return a.second < b.second;
              });

    for (const auto& [macro_index, distance] : macro_indices_with_distances) {
        MacroInfo& macro = _exp_mov_macros[macro_index];
        bool placed = false;

        int start_grid_x = (macro.x - _core.lx) / _gcd_grid_width;
        int end_grid_x = (macro.x + macro.width - _core.lx) / _gcd_grid_width;
        int start_grid_y = (macro.y - _core.ly) / _gcd_grid_height;
        int end_grid_y = (macro.y + macro.height - _core.ly) / _gcd_grid_height;

        int closest_grid_x = -1;
        int closest_grid_y = -1;
        double min_border_distance = std::numeric_limits<double>::max();

        for (int i = start_grid_x; i <= end_grid_x - macro.grid_count_x + 1; ++i) {
            for (int j = start_grid_y; j <= end_grid_y - macro.grid_count_y + 1; ++j) {
                bool can_place = true;
                double grid_border_distance = std::numeric_limits<double>::max();
                
                if (i + macro.grid_count_x > _gcd_num_grid_x || j + macro.grid_count_y > _gcd_num_grid_y) {
                    std::cout << "Skipping grid (" << i << ", " << j << ") due to grid overflow." << std::endl;
                    continue;
                }

                for (int x_offset = 0; x_offset < macro.grid_count_x; ++x_offset) {
                    for (int y_offset = 0; y_offset < macro.grid_count_y; ++y_offset) {
                        if (_gcd_grids[i + x_offset][j + y_offset].is_used) {
                            can_place = false;
                            std::cout << "Grid (" << i << ", " << j << ") already used." << std::endl;
                            break;
                        }
                    }
                    if (!can_place) break;
                }

                if (can_place) {
                    int grid_x_start = _gcd_grids[i][j].x_start;
                    int grid_y_start = _gcd_grids[i][j].y_start;

                    double distance_to_left = static_cast<double>(grid_x_start - _core.lx);
                    double distance_to_right = static_cast<double>(_core.ux - (grid_x_start + macro.grid_count_x * _gcd_grid_width));
                    double distance_to_bottom = static_cast<double>(grid_y_start - _core.ly);
                    double distance_to_top = static_cast<double>(_core.uy - (grid_y_start + macro.grid_count_y * _gcd_grid_height));

                    grid_border_distance = std::min({distance_to_left, distance_to_right, distance_to_bottom, distance_to_top});

                    if (grid_border_distance < min_border_distance) {
                        closest_grid_x = i;
                        closest_grid_y = j;
                        min_border_distance = grid_border_distance;
                    }

                    std::cout << "Checking grid (" << i << ", " << j << "), distance to border: " << grid_border_distance << std::endl;
                }
            }
        }

        if (closest_grid_x != -1 && closest_grid_y != -1) {
            macro.x = _gcd_grids[closest_grid_x][closest_grid_y].x_start;
            macro.y = _gcd_grids[closest_grid_x][closest_grid_y].y_start;

            macro.grid_x = closest_grid_x;
            macro.grid_y = closest_grid_y;

            for (int x_offset = 0; x_offset < macro.grid_count_x; ++x_offset) {
                for (int y_offset = 0; y_offset < macro.grid_count_y; ++y_offset) {
                    _gcd_grids[closest_grid_x + x_offset][closest_grid_y + y_offset].is_used = true;
                    // std::cout << "Marking grid (" << closest_grid_x + x_offset << ", " 
                    //         << closest_grid_y + y_offset << ") as used." << std::endl;
                }
            }

            std::cout << "Successfully placed macro " << macro.name 
                    << " at new position (" << macro.x << ", " << macro.y << ")" << std::endl;
            placed = true;
        }

        assert(placed && "Error: Unable to place macro!");
    }
}

double Refinement::calculateObjectiveFunction() {
    double total_cost = 0.0;

    total_cost += calculateMovement(_mov_macros, _exp_mov_macros);
    total_cost += calculatePeripheralCost(_exp_mov_macros);

    return total_cost;
}

void Refinement::simulatedAnnealingOptimize(int iterations, double temperature, double cooling_rate) {

    double previous_cost = calculateObjectiveFunction();

    for (int iter = 0; iter < iterations; ++iter) {
        // 随机选择一个宏单元
        int macro_index = rand() % _exp_mov_macros.size();
        MacroInfo& macro = _exp_mov_macros[macro_index];

        // 随机选择一个方向：上、下、左、右
        int direction = rand() % 4;  // 0: up, 1: down, 2: left, 3: right
        int new_grid_x = macro.grid_x;
        int new_grid_y = macro.grid_y;

        int old_grid_x = macro.grid_x;
        int old_grid_y = macro.grid_y;
        int old_x = macro.x;
        int old_y = macro.y;

        // 移动宏单元的网格坐标
        if (direction == 0) new_grid_y += 1;  // 向上
        else if (direction == 1) new_grid_y -= 1;  // 向下
        else if (direction == 2) new_grid_x -= 1;  // 向左
        else if (direction == 3) new_grid_x += 1;  // 向右

        // 检查是否超出边界
        if (new_grid_x < 0 || new_grid_x + macro.grid_count_x > _gcd_num_grid_x ||
            new_grid_y < 0 || new_grid_y + macro.grid_count_y > _gcd_num_grid_y) {
            continue;
        }

        // 移动之前，将当前占据的网格的 is_used 置为 false
        for (int x_offset = 0; x_offset < macro.grid_count_x; ++x_offset) {
            for (int y_offset = 0; y_offset < macro.grid_count_y; ++y_offset) {
                _gcd_grids[macro.grid_x + x_offset][macro.grid_y + y_offset].is_used = false;
            }
        }

        // 检查新位置的格子是否已经被使用
        bool can_move = true;
        for (int x_offset = 0; x_offset < macro.grid_count_x; ++x_offset) {
            for (int y_offset = 0; y_offset < macro.grid_count_y; ++y_offset) {
                if (_gcd_grids[new_grid_x + x_offset][new_grid_y + y_offset].is_used) {
                    can_move = false;
                    break;
                }
            }
            if (!can_move) break;
        }

        if (can_move) {
            macro.grid_x = new_grid_x;
            macro.grid_y = new_grid_y;
            macro.x = _gcd_grids[new_grid_x][new_grid_y].x_start;
            macro.y = _gcd_grids[new_grid_x][new_grid_y].y_start;

            for (int x_offset = 0; x_offset < macro.grid_count_x; ++x_offset) {
                for (int y_offset = 0; y_offset < macro.grid_count_y; ++y_offset) {
                    _gcd_grids[new_grid_x + x_offset][new_grid_y + y_offset].is_used = true;
                }
            }

            double new_cost = calculateObjectiveFunction();  // 计算新的目标函数值
            double delta_cost = new_cost - previous_cost;

            std::cout << "Iteration " << iter 
                      << " | Temperature: " << temperature 
                      << " | Delta cost: " << delta_cost 
                      << " | ";

            if (delta_cost < 0 || (rand() / RAND_MAX) < exp(-delta_cost / temperature)) {
                previous_cost = new_cost;
                std::cout << "Accepted" << std::endl;
            } else {
                for (int x_offset = 0; x_offset < macro.grid_count_x; ++x_offset) {
                    for (int y_offset = 0; y_offset < macro.grid_count_y; ++y_offset) {
                        _gcd_grids[new_grid_x + x_offset][new_grid_y + y_offset].is_used = false;
                        _gcd_grids[old_grid_x + x_offset][old_grid_y + y_offset].is_used = true;
                    }
                }
                macro.grid_x = old_grid_x;
                macro.grid_y = old_grid_y;
                macro.x = old_x;
                macro.y = old_y;
                std::cout << "Rejected" << std::endl;
            }
        } else {
            for (int x_offset = 0; x_offset < macro.grid_count_x; ++x_offset) {
                for (int y_offset = 0; y_offset < macro.grid_count_y; ++y_offset) {
                    _gcd_grids[macro.grid_x + x_offset][macro.grid_y + y_offset].is_used = true;
                }
            }
        }

        temperature *= cooling_rate;
    }
}

void Refinement::runRefinement(int method, std::string output_tcl)
{
    std::cout << "Running refinement process..." << std::endl;

    export_to_json("before_refinement.json");

    expandMacros();

    if (method == 0) {
        std::cout << "Running bounding box method..." << std::endl;
    } else if (method == 1) {
        std::cout << "Running MP-tree method..." << std::endl;
    } else if (method == 2) {
        std::cout << "Running grids method..." << std::endl;
        int iterations = 1000;
        double initial_temperature = 2000.0;
        double cooling_rate = 0.95;

        _gcd_grids = divideCoreIntoGridsWithMacroGCD();
        placeMacrosNearBoundaryOptimized();
        simulatedAnnealingOptimize(iterations, initial_temperature, cooling_rate);
        adjustMacroOrientationBasedOnGridPosition();
    }

    double total_move = calculateMovement(_mov_macros, _exp_mov_macros);
    std::cout << "Total movement: " << total_move << std::endl;

    restoreMacros();

    double peripheral_cost_after = calculatePeripheralCost(_mov_macros);
    std::cout << "Peripheral cost after refinement: " << peripheral_cost_after << std::endl;

    export_to_json("after_refinement.json");

}

double Refinement::calculateMovement(const std::vector<MacroInfo>& mov_macros, const std::vector<MacroInfo>& exp_macros) {
    double total_movement = 0.0;

    if (mov_macros.size() != exp_macros.size()) {
        std::cerr << "Error: The number of macros in mov_macros does not match exp_macros." << std::endl;
        return total_movement;
    }

    for (size_t i = 0; i < mov_macros.size(); ++i) {
        const auto& mov_macro = mov_macros[i];
        const auto& exp_macro = exp_macros[i];

        int32_t restored_lx = exp_macro.x + static_cast<int32_t>(_macro_halo);
        int32_t restored_ly = exp_macro.y + static_cast<int32_t>(_macro_halo + _exp_space_y);

        // int32_t restored_ux = restored_lx + exp_macro.width - 2 * static_cast<int32_t>(_macro_halo);
        // int32_t restored_uy = restored_ly + exp_macro.height - 2 * (static_cast<int32_t>(_macro_halo) + _exp_space_y);

        if (exp_macro.orient == _original_pin_dir) {
            // restored_ux -= _exp_space_x;
        } else if (exp_macro.orient == "MX") {
            restored_lx += _exp_space_x;
        }

        int32_t move_x = std::abs(mov_macro.x - restored_lx);
        int32_t move_y = std::abs(mov_macro.y - restored_ly);

        total_movement += static_cast<double>(move_x + move_y);
    }

    return total_movement;
}

double Refinement::calculatePeripheralCost(const std::vector<MacroInfo>& macros) const
{
    double total_peripheral_cost = 0;

    for (const auto& macro : macros) {
        int macro_left = macro.x;
        int macro_bottom = macro.y;
        int macro_right = macro.x + macro.width;
        int macro_top = macro.y + macro.height;

        double distance_to_left = static_cast<double>(macro_left - _core.lx);
        double distance_to_bottom = static_cast<double>(macro_bottom - _core.ly);
        double distance_to_right = static_cast<double>(_core.ux - macro_right);
        double distance_to_top = static_cast<double>(_core.uy - macro_top);

        double min_distance = std::min({distance_to_left, distance_to_bottom, distance_to_right, distance_to_top});

        double bias_penalty = min_distance * min_distance;

        total_peripheral_cost += bias_penalty;
    }

    return total_peripheral_cost;
}

void Refinement::writeTcl(const std::string& tcl_file_path) {
    std::ofstream tcl_file(tcl_file_path);
    if (!tcl_file.is_open()) {
        std::cerr << "Error: Unable to open file " << tcl_file_path << std::endl;
        return;
    }

    for (const auto& macro : _mov_macros) {
        tcl_file << "placeInstance " << macro.name << " "
                 << static_cast<double>(macro.x) / dbu << " " 
                 << static_cast<double>(macro.y) / dbu << " "
                 << macro.orient << "\n";

        tcl_file << "setInstancePlacementStatus -status "
                 << (macro.is_fixed ? "fixed" : "movable") << " -name " << macro.name << "\n";
    }

    tcl_file.close();
    std::cout << "TCL script written to " << tcl_file_path << std::endl;
}

void Refinement::export_to_json(const std::string& filename)
{
    nlohmann::json json_data;

    json_data["core"] = {
        {"lx", _core.lx}, {"ly", _core.ly}, {"ux", _core.ux}, {"uy", _core.uy}
    };

    for (const auto& macro : _mov_macros) {
        json_data["macros"].push_back({
            {"id", macro.id},
            {"name", macro.name},
            {"x", macro.x}, 
            {"y", macro.y}, 
            {"width", macro.width},
            {"height", macro.height},
            {"orientation", macro.orient}
        });
    }

    for (const auto& blockage : _blockages) {
        json_data["blockages"].push_back({
            {"lx", blockage.lx}, {"ly", blockage.ly}, {"ux", blockage.ux}, {"uy", blockage.uy}
        });
    }

    for (const auto& pad : _pads) {
        json_data["pads"].push_back({
            {"id", pad.id},
            {"name", pad.name},
            {"x", pad.x},
            {"y", pad.y}
        });
    }

    std::ofstream file(filename);
    file << json_data.dump(4);
    file.close();

    std::cout << "Data exported to " << filename << std::endl;
}

void Refinement::readTcl(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to open file " << file_path << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string command;
        iss >> command;

        if (command == "placeInstance") {
            std::string macro_name;
            double x, y;
            std::string orientation;
            iss >> macro_name >> x >> y >> orientation;

            bool found = false;
            for (auto& macro : _mov_macros) {
                if (macro.name == macro_name) {
                    macro.x = static_cast<int32_t>(x * dbu);
                    macro.y = static_cast<int32_t>(y * dbu);
                    macro.orient = orientation;
                    found = true;
                    break;
                }
            }

            if (!found) {
                std::cerr << "Warning: Macro " << macro_name << " not found in _macros." << std::endl;
            }
        } else if (command == "setInstancePlacementStatus") {
            std::string status_flag, macro_name, status;
            iss >> status_flag >> status_flag >> status >> status_flag >> macro_name;

        }
    }

    file.close();
}

}  // namespace imp

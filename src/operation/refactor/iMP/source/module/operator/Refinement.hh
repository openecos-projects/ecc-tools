#pragma once
#include <cstddef>
#include <functional>
#include <future>
#include <numeric>
#include <set>
#include <unordered_set>
#include <vector>
#include <string>

#include "Block.hh"
#include "ClusterTimingEvaluator.hh"
#include "IDBParserEngine.hh"
#include "Layout.hh"
#include "Logger.hpp"
#include "Net.hh"
#include "Pin.hh"
#include "thread"

namespace imp {

struct PadInfo {
    std::string id;
    std::string name;
    int32_t x;
    int32_t y;
};

struct CoreInfo {
    int32_t lx;
    int32_t ly;
    int32_t ux;
    int32_t uy;
};

struct MacroInfo {
    std::string id;       // Unique identifier for the macro in original design
    std::string name;
    int32_t x, y;         // Macro position
    int32_t width, height; // Macro dimensions
    std::string orient;    // Orientation (e.g., R0, R90, MX, MY)
    bool is_fixed;         // Whether the macro is fixed
    int grid_count_x;      // Macro occupies how many grids horizontally
    int grid_count_y;     // Macro occupies how many grids vertically
    int grid_x, grid_y;    // Macro position in grid
};

struct BlockageInfo {
    int32_t lx;
    int32_t ly;
    int32_t ux;
    int32_t uy;
};

struct Grid {
    int x_start, y_start;
    bool is_used;
};

class Refinement
{
public:
    Refinement(std::weak_ptr<ParserEngine> parser);

    ~Refinement();

    void initPostProcessingData(
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
    );

    void runRefinement(int method, std::string output_tcl);

    void extractMacroData();

    void extractBlockageData();

    void extractCoreData();

    void extractPadData();

    void export_to_json(const std::string& filename);

    void readTcl(const std::string& tcl_file_path);

    void expandMacros();

    void restoreMacros();

    std::vector<std::vector<Grid>> divideCoreIntoGridsWithMacroGCD();

    double calculatePeripheralCost(const std::vector<MacroInfo>& macros) const;

    double calculateMovement(const std::vector<MacroInfo>& mov_macros, const std::vector<MacroInfo>& exp_macros);

    double calculateObjectiveFunction();

    void simulatedAnnealingOptimize(int iterations, double temperature, double cooling_rate);

    void adjustMacroOrientationBasedOnGridPosition();

    void placeMacrosNearBoundaryOptimized();

    void writeTcl(const std::string& tcl_file_path);

private:

    std::weak_ptr<ParserEngine> _parser;

    Block* _root_block;
    float _macro_halo;
    std::string _original_pin_dir;
    int _exp_space_x;
    int _exp_space_y;
    int _search_space_x;
    int _search_space_y;
    int _gap;
    int _virtual_macro_size;
    bool _beikaobei;
    float _h_weight;
    float _v_weight;
    bool _consider_std;

    std::vector<MacroInfo> _mov_macros;

    std::vector<MacroInfo> _exp_mov_macros;

    std::vector<MacroInfo> _fix_macros;

    std::string orientToString(Orient orient);

    std::vector<BlockageInfo> _blockages;

    CoreInfo _core;

    std::vector<PadInfo> _pads;

    float dbu;

    int _gcd_grid_width, _gcd_grid_height, _gcd_num_grid_x, _gcd_num_grid_y, _gcd_reminder_x, _gcd_reminder_y;

    std::vector<std::vector<Grid>> _gcd_grids;
};

}  // namespace imp

#include <iostream>
#include <cassert>
#include <string>

#include "src/vm/GMLVM.h"

int main() {
    std::cout << "[*] Running GameMaker Studio 1.4 VM & RoR Opcodes Test..." << std::endl;

    GMLVM vm;

    // 1. Test ds_grid creation and region operations (RoR procedural level generation)
    int grid_id = vm.ds_grid_create(100, 100);
    assert(grid_id >= 0);

    vm.ds_grid_set_region(grid_id, 10, 10, 20, 20, 1.0);
    assert(vm.ds_grid_get(grid_id, 15, 15) == 1.0);
    assert(vm.ds_grid_get(grid_id, 5, 5) == 0.0);

    vm.ds_grid_destroy(grid_id);

    // 2. Test spatial instance deactivation region
    vm.register_instance(1001, 50, 50);   // inside region
    vm.register_instance(1002, 500, 500); // outside region

    vm.instance_deactivate_region(0, 0, 100, 100, true, false);
    assert(vm.is_instance_active(1001) == false);
    assert(vm.is_instance_active(1002) == true);

    vm.instance_activate_all();
    assert(vm.is_instance_active(1001) == true);

    // 3. Test Save.ini path redirection to local SD directory
    std::string save_path = vm.resolve_save_file_path("Save.ini");
    assert(save_path == "./Save.ini");

    std::cout << "[+] RoR GMS 1.4 opcodes and VM operations test PASSED." << std::endl;
    return 0;
}

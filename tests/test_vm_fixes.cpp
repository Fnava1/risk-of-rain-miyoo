#include <iostream>
#include <cassert>
#include "src/vm/GMLVM.h"

int main() {
    printf("[*] Running GML VM Priority Queue & Instance Region Fix Test...\n");

    GMLVM vm;

    // 1. Test ds_priority queue (used by RoR director for elite monster spawns)
    int p_id = vm.ds_priority_create();
    assert(p_id >= 0);

    vm.ds_priority_add(p_id, 101, 10.0);
    vm.ds_priority_add(p_id, 102, 50.0);
    vm.ds_priority_add(p_id, 103, 25.0);

    int max_val = vm.ds_priority_delete_max(p_id);
    assert(max_val == 102);

    int next_max = vm.ds_priority_delete_max(p_id);
    assert(next_max == 103);

    vm.ds_priority_destroy(p_id);

    // 2. Test instance_activate_region
    vm.register_instance(2001, 50, 50);
    vm.register_instance(2002, 300, 300);

    // Deactivate all first
    vm.instance_deactivate_region(-1000, -1000, 2000, 2000, true, false);
    assert(vm.is_instance_active(2001) == false);
    assert(vm.is_instance_active(2002) == false);

    // Activate region containing 2001
    vm.instance_activate_region(0, 0, 100, 100, true);
    assert(vm.is_instance_active(2001) == true);
    assert(vm.is_instance_active(2002) == false);

    printf("[+] GML VM Priority queue and activate region test PASSED.\n");
    return 0;
}

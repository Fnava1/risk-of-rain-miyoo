#include "GMLVM.h"

void GMLVM::register_instance(int id, double x, double y) {
    m_instances[id] = GMLInstance{id, x, y, true};
}

void GMLVM::instance_deactivate_region(double left, double top, double width, double height, bool inside, bool notme) {
    (void)notme;
    double right = left + width;
    double bottom = top + height;

    for (auto &pair : m_instances) {
        GMLInstance &inst = pair.second;
        bool is_in = (inst.x >= left && inst.x <= right && inst.y >= top && inst.y <= bottom);
        if (inside) {
            if (is_in) inst.active = false;
        } else {
            if (!is_in) inst.active = false;
        }
    }
}

void GMLVM::instance_activate_all() {
    for (auto &pair : m_instances) {
        pair.second.active = true;
    }
}

bool GMLVM::is_instance_active(int id) const {
    auto it = m_instances.find(id);
    if (it == m_instances.end()) return false;
    return it->second.active;
}

std::string GMLVM::resolve_save_file_path(const std::string &filename) const {
    if (filename == "Save.ini" || filename == "save.ini") {
        return "./Save.ini";
    }
    return filename;
}

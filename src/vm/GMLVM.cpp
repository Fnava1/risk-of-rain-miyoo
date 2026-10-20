#include "GMLVM.h"
#include <algorithm>

int GMLVM::ds_grid_create(int width, int height) {
    if (width <= 0 || height <= 0) return -1;
    int id = m_next_grid_id++;
    m_grids.emplace(id, GMLGrid(width, height));
    return id;
}

void GMLVM::ds_grid_destroy(int grid_id) {
    m_grids.erase(grid_id);
}

void GMLVM::ds_grid_set_region(int grid_id, int x1, int y1, int x2, int y2, double val) {
    auto it = m_grids.find(grid_id);
    if (it == m_grids.end()) return;

    GMLGrid &grid = it->second;
    int min_x = std::max(0, std::min(x1, x2));
    int max_x = std::min(grid.width - 1, std::max(x1, x2));
    int min_y = std::max(0, std::min(y1, y2));
    int max_y = std::min(grid.height - 1, std::max(y1, y2));

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            grid.data[y * grid.width + x] = val;
        }
    }
}

double GMLVM::ds_grid_get(int grid_id, int x, int y) const {
    auto it = m_grids.find(grid_id);
    if (it == m_grids.end()) return 0.0;

    const GMLGrid &grid = it->second;
    if (x < 0 || x >= grid.width || y < 0 || y >= grid.height) {
        return 0.0;
    }
    return grid.data[y * grid.width + x];
}

int GMLVM::ds_priority_create() {
    int id = m_next_priority_id++;
    m_priorities.emplace(id, std::priority_queue<PriorityItem>());
    return id;
}

void GMLVM::ds_priority_destroy(int priority_id) {
    m_priorities.erase(priority_id);
}

void GMLVM::ds_priority_add(int priority_id, int value, double priority) {
    auto it = m_priorities.find(priority_id);
    if (it == m_priorities.end()) return;

    it->second.push(PriorityItem{value, priority});
}

int GMLVM::ds_priority_delete_max(int priority_id) {
    auto it = m_priorities.find(priority_id);
    if (it == m_priorities.end() || it->second.empty()) return -1;

    int val = it->second.top().value;
    it->second.pop();
    return val;
}

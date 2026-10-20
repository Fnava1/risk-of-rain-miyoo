#ifndef GMLVM_H
#define GMLVM_H

#include <vector>
#include <unordered_map>
#include <queue>
#include <string>

struct GMLGrid {
    int width;
    int height;
    std::vector<double> data;

    GMLGrid(int w, int h) : width(w), height(h), data(w * h, 0.0) {}
};

struct GMLInstance {
    int id;
    double x;
    double y;
    bool active;
};

struct PriorityItem {
    int value;
    double priority;

    bool operator<(const PriorityItem &other) const {
        return priority < other.priority; // max-heap
    }
};

class GMLVM {
public:
    GMLVM() = default;
    ~GMLVM() = default;

    // ds_grid operations for procedural level generation
    int ds_grid_create(int width, int height);
    void ds_grid_destroy(int grid_id);
    void ds_grid_set_region(int grid_id, int x1, int y1, int x2, int y2, double val);
    double ds_grid_get(int grid_id, int x, int y) const;

    // ds_priority queue operations for spawn director & elite selections
    int ds_priority_create();
    void ds_priority_destroy(int priority_id);
    void ds_priority_add(int priority_id, int value, double priority);
    int ds_priority_delete_max(int priority_id);

    // Instance activation / spatial deactivation region
    void register_instance(int id, double x, double y);
    void instance_deactivate_region(double left, double top, double width, double height, bool inside, bool notme);
    void instance_activate_region(double left, double top, double width, double height, bool inside);
    void instance_activate_all();
    bool is_instance_active(int id) const;

    // Save and persistence operations
    std::string resolve_save_file_path(const std::string &filename) const;

private:
    std::unordered_map<int, GMLGrid> m_grids;
    int m_next_grid_id = 0;

    std::unordered_map<int, std::priority_queue<PriorityItem>> m_priorities;
    int m_next_priority_id = 0;

    std::unordered_map<int, GMLInstance> m_instances;
};

#endif // GMLVM_H

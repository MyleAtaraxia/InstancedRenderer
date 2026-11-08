#pragma once

#include "godot_cpp/classes/world3d.hpp"
#include "godot_cpp/variant/aabb.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/rid.hpp"
#include "godot_cpp/variant/transform3d.hpp"
#include "godot_cpp/variant/vector3i.hpp"
#include "instance_definition_3d.h"
#include <cstdint>
#include <vector>

namespace godot {

// One definition in one chunk of one world, drawn as a MultiMesh.
class InstancedRenderer3D {
  static constexpr int FLOATS_PER_INSTANCE = 12; // MultiMesh TRANSFORM_3D
  // Fraction of a chunk an instance may stray before changing batch, so ones
  // hovering on a chunk edge don't swap batches every frame.
  static constexpr real_t CELL_MARGIN = 0.25;

  Ref<World3D> world; // keeps the scenario alive
  Ref<InstanceDefinition3D> definition;
  Vector3i cell;
  // Chunk bounds padded by CELL_MARGIN; chunk size 0 means one unbounded chunk.
  Vector3 cell_lo;
  Vector3 cell_hi;
  bool bounded;
  bool frozen; // static or sleeping
  RID multimesh;
  RID instance;
  LodLevels lods;
  int current_lod = 0;
  bool interpolated = false;
  bool flush_queued = false; // in the manager's dirty list

  PackedFloat32Array buffer; // sized to capacity, MultiMesh layout
  int capacity = 0;
  int allocated = 0;      // capacity the RenderingServer currently holds
  int uploaded_count = 0; // visible instance count the RenderingServer holds
  AABB bounds;            // sent as custom AABB
  std::vector<int> owners; // render index -> manager slot

  bool full_dirty = false;
  std::vector<uint8_t> slot_dirty; // per render index
  std::vector<int> dirty_slots;
  std::vector<int> reset_slots; // render indices to snap, not interpolate

  void queue_flush();
  void mark_dirty(int render_index, bool reset);
  void reserve(int count);
  AABB compute_bounds(int count, const AABB &mesh_aabb) const;
  AABB lods_aabb() const;
  void write_transform(int render_index, const Transform3D &t);

public:
  InstancedRenderer3D(const Ref<World3D> &p_world,
                      const Ref<InstanceDefinition3D> &p_definition,
                      const Vector3i &p_cell, bool p_frozen,
                      int initial_capacity);
  ~InstancedRenderer3D();

  RID get_scenario() const { return world->get_scenario(); }
  const Ref<World3D> &get_world() const { return world; }
  const Ref<InstanceDefinition3D> &get_definition() const { return definition; }
  const Vector3i &get_cell() const { return cell; }
  bool in_cell(const Vector3 &p) const {
    return !bounded || (p.x > cell_lo.x && p.x < cell_hi.x && p.y > cell_lo.y &&
                        p.y < cell_hi.y && p.z > cell_lo.z && p.z < cell_hi.z);
  }
  bool has_lods() const { return lods.size() > 1; }
  bool is_interpolated() const { return interpolated; }
  bool is_flush_queued() const { return flush_queued; }
  int get_instance_count() const { return (int)owners.size(); }
  int get_owner(int render_index) const { return owners[render_index]; }
  Transform3D read_transform(int render_index) const;

  int add(int owner, const Transform3D &t);
  void set_transform(int render_index, const Transform3D &t);
  void set_origin(int render_index, const Vector3 &origin);
  // FLOATS_PER_INSTANCE floats in MultiMesh layout.
  void set_packed(int render_index, const float *src);
  // Returns the slot that moved into render_index, or -1.
  int remove(int render_index);
  void reset_interpolation(int render_index);
  void set_interpolation(bool p_interpolated, int quality);
  // Picks the level by camera distance to the batch's bounds centre.
  void update_lod(const Vector3 &camera_position);
  void flush();
};

} // namespace godot

#pragma once

#include "godot_cpp/classes/material.hpp"
#include "godot_cpp/classes/mesh.hpp"
#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/variant/aabb.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_int64_array.hpp"
#include "godot_cpp/variant/rid.hpp"
#include "godot_cpp/variant/transform3d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include <cstdint>
#include <utility>
#include <vector>

namespace godot {

// (start distance, mesh), sorted; level 0 is the base mesh at distance 0.
using LodLevels = std::vector<std::pair<real_t, Ref<Mesh>>>;

// Instance id = generation << 32 | slot; a stale generation rejects the id.
struct InstanceRecord {
  int render_index = -1;
  uint32_t generation = 0;
  bool active = false;
};

class InstancedRenderer3D : public RefCounted {
  GDCLASS(InstancedRenderer3D, RefCounted)

private:
  static constexpr int FLOATS_PER_INSTANCE = 12; // MultiMesh TRANSFORM_3D

  RID multimesh;
  RID instance;
  LodLevels lods;
  int current_lod = 0;
  RID scenario;
  Ref<Material> material;
  bool interpolated = false;
  bool is_static = false;
  bool flush_queued = false; // in the manager's dirty list

  PackedFloat32Array buffer; // sized to capacity
  int capacity = 0;
  int allocated = 0;      // capacity the RenderingServer currently holds
  int uploaded_count = 0; // visible instance count the RenderingServer holds
  AABB bounds;            // sent as custom AABB

  std::vector<InstanceRecord> records; // slot -> record
  std::vector<int> render_ids;         // render index -> slot
  std::vector<int> free_ids;

  bool full_dirty = false;
  std::vector<uint8_t> slot_dirty; // per render index
  std::vector<int> dirty_slots;
  std::vector<int> reset_slots; // render indices to snap, not interpolate

  void queue_flush();
  void mark_dirty(int render_index, bool reset);
  AABB compute_bounds(int count, const AABB &mesh_aabb) const;
  AABB lods_aabb() const;
  void write_transform(int render_index, const Transform3D &t);
  Transform3D read_transform(int render_index) const;
  int slot_of(int64_t id) const; // -1 if the id is stale or invalid

protected:
  static void _bind_methods();

public:
  InstancedRenderer3D();
  ~InstancedRenderer3D();

  void setup(const RID &p_scenario, const LodLevels &p_lods,
             const Ref<Material> &p_material, int cast_shadow, uint32_t layers,
             bool p_static, real_t visibility_range_end, int initial_capacity);
  bool has_lods() const { return lods.size() > 1; }
  RID get_scenario() const { return scenario; }
  // Picks the level by camera distance to the batch's bounds centre.
  void update_lod(const Vector3 &camera_position);
  void set_interpolation(bool p_interpolated, int quality);
  bool is_interpolated() const { return interpolated; }
  void flush();

  int64_t add_instance(const Transform3D &transform);
  void update_instance(int64_t id, const Transform3D &transform);
  // One transform per id.
  void update_instances(const PackedInt64Array &ids,
                        const TypedArray<Transform3D> &transforms);
  void remove_instance(int64_t id);
  void reset_instance_interpolation(int64_t id);
  bool is_instance_valid(int64_t id) const;
  int get_instance_count() const;

  void clear_instances();
  void set_estimated_instance_count(int count);
};

} // namespace godot

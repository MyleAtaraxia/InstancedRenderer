#pragma once

#include "godot_cpp/classes/world3d.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_int64_array.hpp"
#include "godot_cpp/variant/packed_vector3_array.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "instanced_renderer_3d.h"
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace godot {

// Engine singleton owning every 3D instance. Ids stay valid across chunk
// crossings, sleeping and definition edits. Transforms are in world space.
class InstanceManager3D : public Object {
  GDCLASS(InstanceManager3D, Object)

  static InstanceManager3D *singleton;

  // Instance id = generation << 32 | slot; a stale generation rejects the id.
  struct Slot {
    InstancedRenderer3D *renderer = nullptr;
    int render_index = -1;
    uint32_t generation = 0;
    uint64_t last_moved_frame = 0;
    bool rest = false; // in a frozen batch
    bool is_static = false;
    bool sleep_watched = false;
  };
  // scenario, definition, cell x/y/z, rest
  using Key = std::tuple<uint64_t, uint64_t, int32_t, int32_t, int32_t, bool>;
  // Mesh, material and LOD ids, cast shadow, layers, visibility range end,
  // chunk size.
  using SharedKey = std::tuple<uint64_t, uint64_t, std::vector<uint64_t>, int,
                               uint32_t, real_t, real_t>;

  std::map<Key, std::unique_ptr<InstancedRenderer3D>> renderers;
  std::vector<InstancedRenderer3D *> lod_renderers; // owned by `renderers`
  std::map<SharedKey, uint64_t> shared_definitions; // weak, by object id
  std::vector<Slot> slots;
  std::vector<int> free_slots;
  std::vector<int64_t> sleep_watch;
  std::vector<InstancedRenderer3D *> dirty_frame;
  std::vector<InstancedRenderer3D *> dirty_physics;
  Vector3 last_camera_position;
  bool lods_dirty = false;
  bool flush_queued = false;
  bool any_empty = false;

  // Project settings, reloaded live
  int interpolation_mode = 0;
  int interpolation_quality = 0;
  int initial_capacity = 16;
  int sleep_after_frames = 60;

  int slot_of(int64_t id) const; // -1 if the id is stale or invalid
  InstancedRenderer3D *get_renderer(const Ref<World3D> &world,
                                    const Ref<InstanceDefinition3D> &definition,
                                    const Vector3 &origin, bool rest);
  void detach(Slot &slot);
  void place(int slot, const Ref<World3D> &world,
             const Ref<InstanceDefinition3D> &definition, const Transform3D &t);
  void move(int slot, const Transform3D &t);
  bool needs_move(const Slot &slot, const Vector3 &origin) const;
  void update_slot(int slot, const Transform3D &t, uint64_t now);
  void mark_moved(Slot &slot, int64_t id, uint64_t now);
  void forget(InstancedRenderer3D *renderer);
  void free_slot(int slot);
  void on_definition_changed(uint64_t definition_id);

  void connect_signals();
  void load_settings();
  void update_sleep();
  void update_lods();
  void drop_empty_renderers();
  void flush_frame();
  void queue_physics_flush();
  void flush_physics();
  void flush(bool p_interpolated);

protected:
  static void _bind_methods();

public:
  static constexpr const char *SETTING_DEFAULT_CHUNK_SIZE =
      "instanced_renderer/3d/default_chunk_size";
  static void register_project_settings();

  static InstanceManager3D *get_singleton() { return singleton; }

  InstanceManager3D();
  ~InstanceManager3D();

  int64_t add_instance(const Ref<World3D> &world,
                       const Ref<InstanceDefinition3D> &definition,
                       const Transform3D &transform, bool is_static = false);
  void update_instance(int64_t id, const Transform3D &transform);
  // One transform per id.
  void update_instances(const PackedInt64Array &ids,
                        const TypedArray<Transform3D> &transforms);
  // One position per id; keeps each instance's rotation and scale.
  void update_instance_positions(const PackedInt64Array &ids,
                                 const PackedVector3Array &positions);
  // One Euler rotation in radians (YXZ, like Node3D.rotation) per id; keeps
  // each instance's position and scale.
  void update_instance_rotations(const PackedInt64Array &ids,
                                 const PackedVector3Array &rotations);
  // 12 floats per id, MultiMesh TRANSFORM_3D layout: each basis row followed
  // by its origin component.
  void update_instances_packed(const PackedInt64Array &ids,
                               const PackedFloat32Array &transforms);
  void remove_instance(int64_t id);
  void reset_instance_interpolation(int64_t id);
  bool is_instance_valid(int64_t id) const;
  int get_batch_count() const { return (int)renderers.size(); }
  Ref<InstanceDefinition3D> share_definition(
      const Ref<Mesh> &mesh, const Ref<Material> &material,
      const TypedArray<InstancedMeshLOD> &lods, int cast_shadow,
      uint32_t layers, real_t visibility_range_end, real_t chunk_size);

  void queue_flush(InstancedRenderer3D *renderer);
};

} // namespace godot

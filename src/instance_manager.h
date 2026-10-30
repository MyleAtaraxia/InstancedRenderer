#pragma once

#include "godot_cpp/classes/world3d.hpp"
#include "godot_cpp/variant/color.hpp"
#include "godot_cpp/variant/vector3i.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "instanced_mesh_lod.h"
#include "instanced_renderer_3d.h"
#include <map>
#include <tuple>

namespace godot {

void add_setting(const String &name, const Variant &default_value,
                 PropertyHint hint = PROPERTY_HINT_NONE,
                 const String &hint_string = "");

// Engine singleton owning one InstancedRenderer3D per batch key.
class InstanceManager : public Object {
  GDCLASS(InstanceManager, Object)

  static InstanceManager *singleton;

  // scenario, mesh, material RIDs, cast shadow, layers, chunk size, static,
  // sleeping, visibility range end, LOD (distance, mesh RID) list, cell x/y/z
  using Key = std::tuple<int64_t, int64_t, int64_t, int, uint32_t, real_t,
                         bool, bool, real_t,
                         std::vector<std::pair<real_t, int64_t>>, int32_t,
                         int32_t, int32_t>;
  std::map<Key, Ref<InstancedRenderer3D>> renderers;
  std::vector<InstancedRenderer3D *> lod_renderers; // owned by `renderers`
  Vector3 last_camera_position;
  bool lods_dirty = false;
  std::vector<Ref<InstancedRenderer3D>> dirty_frame;
  std::vector<Ref<InstancedRenderer3D>> dirty_physics;
  bool flush_queued = false;
  // InstancedMesh3D ObjectIDs
  std::vector<uint64_t> sleep_watch;

  // Project settings, reloaded live
  int interpolation_mode = 0; // InterpolationMode
  int interpolation_quality = 0;
  int initial_capacity = 16;
  int sleep_after_frames = 60;

  void connect_signals();
  void load_settings();
  bool should_interpolate() const;
  void update_sleep();
  void update_lods();
  void flush_frame();
  void queue_physics_flush();
  void flush_physics();
  void flush(bool p_interpolated);

protected:
  static void _bind_methods();

public:
  // No "always": the engine only ticks interpolation when the project enables it.
  enum InterpolationMode {
    INTERPOLATION_FOLLOW_PROJECT,
    INTERPOLATION_DISABLED,
  };

  static constexpr const char *SETTING_INTERPOLATION_MODE =
      "instanced_renderer/interpolation/mode";
  static constexpr const char *SETTING_INTERPOLATION_QUALITY =
      "instanced_renderer/interpolation/quality";
  static constexpr const char *SETTING_INITIAL_CAPACITY =
      "instanced_renderer/buffer/initial_capacity";
  static constexpr const char *SETTING_DEFAULT_CHUNK_SIZE =
      "instanced_renderer/batching/default_chunk_size";
  static constexpr const char *SETTING_SLEEP_AFTER_FRAMES =
      "instanced_renderer/batching/sleep_after_frames";
  static constexpr const char *SETTING_GIZMO_COLOR =
      "instanced_renderer/editor/gizmo_color";
  static void register_project_settings();

  static InstanceManager *get_singleton() { return singleton; }

  InstanceManager();
  ~InstanceManager();

  static Vector3i get_cell(const Vector3 &position, real_t chunk_size);

  // Sorted, without levels at or past visibility_range_end, base mesh first.
  static LodLevels make_lod_levels(const Ref<Mesh> &mesh,
                                   const TypedArray<InstancedMeshLOD> &lods,
                                   real_t visibility_range_end);

  Ref<InstancedRenderer3D> get_renderer(const Ref<World3D> &world,
                                        const LodLevels &lods,
                                        const Ref<Material> &material,
                                        const Vector3i &cell,
                                        real_t chunk_size, int cast_shadow,
                                        uint32_t layers, bool is_static,
                                        real_t visibility_range_end,
                                        bool sleeping);
  Ref<InstancedRenderer3D> get_renderer_bind(
      const Ref<World3D> &world, const Ref<Mesh> &mesh,
      const Ref<Material> &material, const Vector3i &cell, real_t chunk_size,
      int cast_shadow, uint32_t layers, bool is_static,
      real_t visibility_range_end, bool sleeping,
      const TypedArray<InstancedMeshLOD> &lods);
  // Called once per renderer between flushes.
  void queue_flush(InstancedRenderer3D *renderer);
  void watch_for_sleep(uint64_t node_id) { sleep_watch.push_back(node_id); }
  int get_sleep_after_frames() const { return sleep_after_frames; }
};

} // namespace godot

#pragma once

#include "godot_cpp/classes/canvas_item.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_int64_array.hpp"
#include "instanced_renderer_2d.h"
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace godot {

// Engine singleton owning every 2D instance. Ids stay valid across chunk
// crossings, sleeping and definition edits.
class InstanceManager2D : public Object {
  GDCLASS(InstanceManager2D, Object)

  static InstanceManager2D *singleton;

  // Instance id = generation << 32 | slot; a stale generation rejects the id.
  struct Slot {
    InstancedRenderer2D *renderer = nullptr;
    int render_index = -1;
    uint32_t generation = 0;
    uint64_t last_moved_frame = 0;
    bool rest = false; // in a frozen batch
    bool is_static = false;
    bool sleep_watched = false;
  };
  // parent, definition, cell x/y, rest
  using Key = std::tuple<uint64_t, uint64_t, int32_t, int32_t, bool>;
  using RectKey = std::tuple<real_t, real_t, real_t, real_t>;
  struct Quad {
    RID mesh;
    int references = 0;
  };

  std::map<Key, std::unique_ptr<InstancedRenderer2D>> renderers;
  std::map<uint64_t, InstanceParent2D> parents;
  std::map<RectKey, Quad> quads;
  std::vector<RID> item_pool; // unparented: Y-sort still walks hidden children
  int live_items = 0;

  std::vector<Slot> slots;
  std::vector<int> free_slots;
  std::vector<int64_t> sleep_watch;

  // Mesh, texture and material ids, offset, modulate, y-sorted, z index, chunk
  // size, pixels per meter, texture filter and repeat.
  using SharedKey = std::tuple<uint64_t, uint64_t, uint64_t, real_t, real_t, float,
                               float, float, float, bool, int, real_t, real_t, int, int>;
  std::map<SharedKey, uint64_t> shared_definitions; // weak, by object id
  std::vector<InstancedRenderer2D *> dirty_frame;
  std::vector<InstancedRenderer2D *> dirty_physics;
  struct VisibleCount {
    InstancedRenderer2D *renderer;
    int count;
  };
  // Applied on the frame after the one they were queued on.
  std::vector<VisibleCount> counts_waiting;
  std::vector<VisibleCount> counts_due;
#ifdef INSTANCED_2D_CANVAS_ECHO
  std::vector<InstancedRenderer2D *> echoes;
  bool canvas_echo = true;
#endif
  bool flush_queued = false;
  bool any_empty = false;

  // Shared with InstanceManager's settings, reloaded live
  int interpolation_mode = 0;
  int initial_capacity = 16;
  int sleep_after_frames = 60;

  int slot_of(int64_t id) const; // -1 if the id is stale or invalid
  InstancedRenderer2D *get_renderer(InstanceParent2D &parent,
                                    const Ref<InstanceDefinition2D> &definition,
                                    const Vector2 &origin, bool rest);
  void detach(Slot &slot);
  void place(int slot, InstanceParent2D &parent,
             const Ref<InstanceDefinition2D> &definition, const Transform2D &t);
  void move(int slot, const Transform2D &t);
  void update_slot(int slot, const Transform2D &t);
  void mark_moved(Slot &slot, int64_t id);
  void forget_dirty(InstancedRenderer2D *renderer);
  void free_slot(int slot);
  void on_definition_changed(uint64_t definition_id);

  void connect_signals();
  void load_settings();
  bool should_interpolate() const;
  void update_sleep();
  void update_views();
  void drop_empty_renderers();
  void flush_frame();
#ifdef INSTANCED_2D_CANVAS_ECHO
  void flush_echoes();
#endif
  void queue_physics_flush();
  void flush_physics();
  void flush(bool p_interpolated);

protected:
  static void _bind_methods();

public:
  static constexpr const char *SETTING_DEFAULT_CHUNK_SIZE =
      "instanced_renderer/batching/default_chunk_size_2d";
  static constexpr const char *SETTING_BAKE_STATIC =
      "instanced_renderer/2d/bake_static_on_export";
#ifdef INSTANCED_2D_CANVAS_ECHO
  static constexpr const char *SETTING_CANVAS_ECHO =
      "instanced_renderer/2d/repeat_uploads_for_motion_vectors";
#endif
  static void register_project_settings();

  static InstanceManager2D *get_singleton() { return singleton; }

  InstanceManager2D();
  ~InstanceManager2D();

  // Transforms are relative to parent.
  int64_t add_instance(CanvasItem *parent,
                       const Ref<InstanceDefinition2D> &definition,
                       const Transform2D &transform, bool is_static = false);
  void update_instance(int64_t id, const Transform2D &transform);
  // 8 floats per id, MultiMesh layout: each basis row, padded, followed by its
  // origin component.
  void update_instances(const PackedInt64Array &ids,
                        const PackedFloat32Array &transforms);
  void remove_instance(int64_t id);
  void reset_instance_interpolation(int64_t id);
  bool is_instance_valid(int64_t id) const;
  // Canvas items currently showing Y-sorted instances.
  int get_rendered_item_count() const { return live_items; }
  int get_batch_count() const { return (int)renderers.size(); }
  Ref<InstanceDefinition2D> share_definition(
      const Ref<Mesh> &mesh, const Ref<Texture2D> &texture,
      const Ref<Material> &material, const Vector2 &offset, const Color &modulate,
      bool y_sorted, int z_index, real_t chunk_size, real_t pixels_per_meter,
      int texture_filter, int texture_repeat);

  RID acquire_quad(const Rect2 &rect);
  void release_quad(const Rect2 &rect);
  RID acquire_item();
  void release_item(const RID &item);
  void queue_flush(InstancedRenderer2D *renderer);
  void queue_visible_count(InstancedRenderer2D *renderer, int count);
  void forget_visible_counts(InstancedRenderer2D *renderer);
#ifdef INSTANCED_2D_CANVAS_ECHO
  void queue_echo(InstancedRenderer2D *renderer) { echoes.push_back(renderer); }
  bool is_canvas_echo_enabled() const { return canvas_echo; }
#endif
};

} // namespace godot

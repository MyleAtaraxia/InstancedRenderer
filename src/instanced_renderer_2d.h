#pragma once

#include "godot_cpp/classes/ref.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/rect2.hpp"
#include "godot_cpp/variant/rid.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/vector2i.hpp"
#include "instance_definition_2d.h"
#include <cstdint>
#include <vector>

namespace godot {

class InstancedRenderer2D;

// Canvas item that instances are attached to.
struct InstanceParent2D {
  uint64_t id = 0; // CanvasItem ObjectID
  RID item;
  RID sort_root; // sort_y; nested under a Y-sorted parent it flattens into it
  Rect2 view;    // in parent space
  bool view_dirty = true;
  int renderer_count = 0;
  std::vector<InstancedRenderer2D *> y_sorted;
};

// One definition in one chunk. Y-sorted chunks hold plain transforms and only
// own canvas items while on screen.
class InstancedRenderer2D {
  static constexpr int FLOATS_PER_INSTANCE = 8; // MultiMesh TRANSFORM_2D

  InstanceParent2D *parent;
  Ref<InstanceDefinition2D> definition;
  Vector2i cell;
  bool y_sorted;
  bool frozen; // static or sleeping
  bool interpolated = false;
  bool flush_queued = false; // in the manager's dirty list
  bool on_screen = false;
  Rect2 local_rect;
  real_t mesh_scale; // baked into the buffer, not into stored transforms
  Rect2 bounds; // parent space

  PackedFloat32Array buffer; // sized to capacity, MultiMesh layout
  int capacity = 0;
  std::vector<int> owners; // render index -> manager slot

  // Y-sorted
  std::vector<RID> items; // render index -> canvas item, empty when off screen

  // Chunked
  RID item;
  RID multimesh;
  bool uses_quad = false;
  int allocated = 0;      // capacity the RenderingServer currently holds
  int uploaded_count = 0; // visible instance count the RenderingServer holds
  bool full_dirty = false;
  std::vector<uint8_t> slot_dirty; // per render index
  std::vector<int> dirty_slots;
  std::vector<int> reset_slots; // render indices to snap, not interpolate
  void queue_flush();
  void mark_dirty(int render_index, bool reset);
  void reserve(int count);
  void grow_bounds(const Transform2D &t);
  Rect2 compute_bounds() const;
  void write_transform(int render_index, const Transform2D &t);
  void upload_slot(int render_index);
  RID show_instance(int render_index);

public:
  InstancedRenderer2D(InstanceParent2D *p_parent,
                      const Ref<InstanceDefinition2D> &p_definition,
                      const Vector2i &p_cell, bool p_frozen,
                      int initial_capacity);
  ~InstancedRenderer2D();

  InstanceParent2D *get_parent() const { return parent; }
  const Ref<InstanceDefinition2D> &get_definition() const { return definition; }
  Vector2i get_cell() const { return cell; }
  bool is_y_sorted() const { return y_sorted; }
  bool is_interpolated() const { return interpolated; }
  bool is_flush_queued() const { return flush_queued; }
  Rect2 get_bounds() const { return bounds; }
  int get_instance_count() const { return (int)owners.size(); }
  int get_owner(int render_index) const { return owners[render_index]; }
  Transform2D read_transform(int render_index) const;

  int add(int owner, const Transform2D &t);
  void set_transform(int render_index, const Transform2D &t);
  // Returns the slot that moved into render_index, or -1.
  int remove(int render_index);
  void reset_interpolation(int render_index);
  void set_interpolation(bool p_interpolated);
  void set_on_screen(bool p_on_screen);
  void flush();
  void set_visible_count(int count);
};

} // namespace godot

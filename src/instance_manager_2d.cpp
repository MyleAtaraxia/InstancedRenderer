#include "instance_manager_2d.h"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/project_settings.hpp"
#include "godot_cpp/classes/rendering_server.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/classes/viewport.hpp"
#include "godot_cpp/core/print_string.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "instance_manager.h"
#include <algorithm>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

InstanceManager2D *InstanceManager2D::singleton = nullptr;

void InstanceManager2D::_bind_methods() {
  ClassDB::bind_method(
      D_METHOD("add_instance", "parent", "definition", "transform", "is_static"),
      &InstanceManager2D::add_instance, DEFVAL(false));
  ClassDB::bind_method(D_METHOD("update_instance", "id", "transform"),
                       &InstanceManager2D::update_instance);
  ClassDB::bind_method(D_METHOD("update_instances", "ids", "transforms"),
                       &InstanceManager2D::update_instances);
  ClassDB::bind_method(D_METHOD("remove_instance", "id"),
                       &InstanceManager2D::remove_instance);
  ClassDB::bind_method(D_METHOD("reset_instance_interpolation", "id"),
                       &InstanceManager2D::reset_instance_interpolation);
  ClassDB::bind_method(D_METHOD("is_instance_valid", "id"),
                       &InstanceManager2D::is_instance_valid);
  ClassDB::bind_method(D_METHOD("get_rendered_item_count"),
                       &InstanceManager2D::get_rendered_item_count);
  ClassDB::bind_method(D_METHOD("get_batch_count"),
                       &InstanceManager2D::get_batch_count);
}

void InstanceManager2D::register_project_settings() {
  add_setting(SETTING_DEFAULT_CHUNK_SIZE, 256.0, PROPERTY_HINT_RANGE,
              "0,4096,1,or_greater,suffix:px");
#ifdef INSTANCED_2D_CANVAS_ECHO
  add_setting(SETTING_CANVAS_ECHO, true);
#endif
}

InstanceManager2D::InstanceManager2D() {
  singleton = this;
  load_settings();
  ProjectSettings::get_singleton()->connect(
      "settings_changed", callable_mp(this, &InstanceManager2D::load_settings));
}

InstanceManager2D::~InstanceManager2D() {
  ProjectSettings::get_singleton()->disconnect(
      "settings_changed", callable_mp(this, &InstanceManager2D::load_settings));
  // At extension shutdown the RenderingServer may already be gone (with our RIDs).
  if (Engine::get_singleton()->has_singleton("RenderingServer")) {
    renderers.clear();
    RenderingServer *rs = RenderingServer::get_singleton();
    for (const RID &rid : item_pool) {
      rs->free_rid(rid);
    }
    for (auto &[key, quad] : quads) {
      rs->free_rid(quad.mesh);
    }
    for (auto &[id, parent] : parents) {
      if (parent.sort_root.is_valid()) {
        rs->free_rid(parent.sort_root);
      }
    }
  } else {
    for (auto &[key, renderer] : renderers) {
      renderer.release();
    }
  }
  singleton = nullptr;
}

void InstanceManager2D::load_settings() {
  ProjectSettings *ps = ProjectSettings::get_singleton();
  interpolation_mode = ps->get_setting(InstanceManager::SETTING_INTERPOLATION_MODE, 0);
  initial_capacity = ps->get_setting(InstanceManager::SETTING_INITIAL_CAPACITY, 16);
  sleep_after_frames = ps->get_setting(InstanceManager::SETTING_SLEEP_AFTER_FRAMES, 60);
#ifdef INSTANCED_2D_CANVAS_ECHO
  canvas_echo = ps->get_setting(SETTING_CANVAS_ECHO, true);
#endif
  bool interpolate = should_interpolate();
  for (auto &[key, r] : renderers) {
    r->set_interpolation(interpolate);
  }
}

bool InstanceManager2D::should_interpolate() const {
  if (Engine::get_singleton()->is_editor_hint()) {
    return false; // the editor never runs physics interpolation
  }
  if (interpolation_mode == InstanceManager::INTERPOLATION_DISABLED) {
    return false;
  }
  SceneTree *tree =
      Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
  return tree && tree->is_physics_interpolation_enabled();
}

// Lazy: the RenderingServer and SceneTree don't exist yet at extension init.
void InstanceManager2D::connect_signals() {
  Callable on_draw = callable_mp(this, &InstanceManager2D::flush_frame);
  RenderingServer *rs = RenderingServer::get_singleton();
  if (!rs->is_connected("frame_pre_draw", on_draw)) {
    rs->connect("frame_pre_draw", on_draw);
  }
#ifdef INSTANCED_2D_CANVAS_ECHO
  // After the draw it's a change like any other, so low processor mode redraws.
  Callable on_drawn = callable_mp(this, &InstanceManager2D::flush_echoes);
  if (!rs->is_connected("frame_post_draw", on_drawn)) {
    rs->connect("frame_post_draw", on_drawn);
  }
#endif
  SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
  Callable on_physics = callable_mp(this, &InstanceManager2D::queue_physics_flush);
  if (tree && !tree->is_connected("physics_frame", on_physics)) {
    tree->connect("physics_frame", on_physics);
  }
}

int InstanceManager2D::slot_of(int64_t id) const {
  int64_t slot = id & 0xFFFFFFFF;
  uint32_t generation = (uint32_t)(id >> 32);
  if (id < 0 || slot >= (int64_t)slots.size() || !slots[slot].renderer ||
      slots[slot].generation != generation) {
    return -1;
  }
  return (int)slot;
}

bool InstanceManager2D::is_instance_valid(int64_t id) const {
  return slot_of(id) >= 0;
}

static Vector2i cell_of(const Vector2 &origin, real_t chunk_size) {
  return chunk_size > 0 ? Vector2i((origin / chunk_size).floor()) : Vector2i();
}

InstancedRenderer2D *InstanceManager2D::get_renderer(
    InstanceParent2D &parent, const Ref<InstanceDefinition2D> &definition,
    const Vector2 &origin, bool rest) {
  Vector2i cell = cell_of(origin, definition->get_chunk_size());
  Key key{parent.id, definition->get_instance_id(), cell.x, cell.y, rest};
  std::unique_ptr<InstancedRenderer2D> &r = renderers[key];
  if (!r) {
    if (definition->is_y_sorted() && !parent.sort_root.is_valid()) {
      RenderingServer *rs = RenderingServer::get_singleton();
      parent.sort_root = rs->canvas_item_create();
      rs->canvas_item_set_parent(parent.sort_root, parent.item);
      rs->canvas_item_set_sort_children_by_y(parent.sort_root, true);
    }
    r = std::make_unique<InstancedRenderer2D>(&parent, definition, cell, rest,
                                              initial_capacity);
    r->set_interpolation(should_interpolate());
    Callable on_changed = callable_mp(this, &InstanceManager2D::on_definition_changed)
                              .bind(definition->get_instance_id());
    if (!definition->is_connected("changed", on_changed)) {
      definition->connect("changed", on_changed);
    }
  }
  return r.get();
}

int64_t InstanceManager2D::add_instance(
    CanvasItem *parent, const Ref<InstanceDefinition2D> &definition,
    const Transform2D &transform, bool is_static) {
  ERR_FAIL_NULL_V(parent, -1);
  ERR_FAIL_COND_V(definition.is_null(), -1);
  connect_signals();

  InstanceParent2D &entry = parents[parent->get_instance_id()];
  if (entry.id == 0) {
    entry.id = parent->get_instance_id();
    entry.item = parent->get_canvas_item();
  }

  int index;
  if (!free_slots.empty()) {
    index = free_slots.back();
    free_slots.pop_back();
  } else {
    index = (int)slots.size();
    slots.emplace_back();
  }
  Slot &slot = slots[index];
  slot.is_static = is_static;
  slot.rest = is_static;
  slot.sleep_watched = false;
  place(index, entry, definition, transform);
  int64_t id = ((int64_t)slot.generation << 32) | index;
  mark_moved(slot, id); // instances that never move fall asleep too
  return id;
}

template <typename T> static uint64_t id_of(const Ref<T> &resource) {
  return resource.is_valid() ? (uint64_t)resource->get_instance_id() : 0;
}

static InstanceDefinition2D *live_definition(uint64_t id) {
  return Object::cast_to<InstanceDefinition2D>(ObjectDB::get_instance(id));
}

// Nodes have no resource to share, so equal settings resolve to one definition.
// Held weakly: the batches using it keep it alive.
Ref<InstanceDefinition2D> InstanceManager2D::share_definition(
    const Ref<Mesh> &mesh, const Ref<Texture2D> &texture,
    const Ref<Material> &material, const Vector2 &offset, const Color &modulate,
    bool y_sorted, int z_index, real_t chunk_size, real_t pixels_per_meter,
    int texture_filter, int texture_repeat) {
  SharedKey key{id_of(mesh), id_of(texture), id_of(material), offset.x, offset.y,
                modulate.r, modulate.g, modulate.b, modulate.a,
                y_sorted, z_index, chunk_size, pixels_per_meter,
                texture_filter, texture_repeat};
  auto found = shared_definitions.find(key);
  if (found != shared_definitions.end()) {
    if (InstanceDefinition2D *live = live_definition(found->second)) {
      return Ref<InstanceDefinition2D>(live);
    }
  }
  for (auto it = shared_definitions.begin(); it != shared_definitions.end();) {
    it = live_definition(it->second) ? std::next(it) : shared_definitions.erase(it);
  }
  Ref<InstanceDefinition2D> definition;
  definition.instantiate();
  definition->set_mesh(mesh);
  definition->set_texture(texture);
  definition->set_material(material);
  definition->set_offset(offset);
  definition->set_modulate(modulate);
  definition->set_y_sorted(y_sorted);
  definition->set_z_index(z_index);
  definition->set_chunk_size(chunk_size);
  definition->set_pixels_per_meter(pixels_per_meter);
  definition->set_texture_filter(texture_filter);
  definition->set_texture_repeat(texture_repeat);
  shared_definitions[key] = definition->get_instance_id();
  return definition;
}

void InstanceManager2D::detach(Slot &slot) {
  int moved = slot.renderer->remove(slot.render_index);
  if (moved >= 0) {
    slots[moved].render_index = slot.render_index;
  }
  any_empty = any_empty || slot.renderer->get_instance_count() == 0;
  slot.renderer = nullptr;
  slot.render_index = -1;
}

void InstanceManager2D::place(int index, InstanceParent2D &parent,
                              const Ref<InstanceDefinition2D> &definition,
                              const Transform2D &t) {
  Slot &slot = slots[index];
  slot.renderer = get_renderer(parent, definition, t.get_origin(), slot.rest);
  slot.render_index = slot.renderer->add(index, t);
}

void InstanceManager2D::move(int index, const Transform2D &t) {
  Slot &slot = slots[index];
  InstanceParent2D *parent = slot.renderer->get_parent();
  Ref<InstanceDefinition2D> definition = slot.renderer->get_definition();
  detach(slot);
  place(index, *parent, definition, t);
}

void InstanceManager2D::mark_moved(Slot &slot, int64_t id) {
  slot.last_moved_frame = Engine::get_singleton()->get_process_frames();
  // Y-sorted instances share no buffer, so sleeping has nothing to save.
  if (!slot.sleep_watched && !slot.is_static && sleep_after_frames > 0 &&
      !slot.renderer->is_y_sorted()) {
    slot.sleep_watched = true;
    sleep_watch.push_back(id);
  }
}

void InstanceManager2D::update_slot(int index, const Transform2D &t) {
  Slot &slot = slots[index];
  InstancedRenderer2D *r = slot.renderer;
  Vector2i cell = cell_of(t.get_origin(), r->get_definition()->get_chunk_size());
  bool wake = slot.rest && !slot.is_static;
  slot.rest = slot.is_static;
  if (wake || cell != r->get_cell()) {
    move(index, t); // first tick snaps if interpolated
  } else {
    r->set_transform(slot.render_index, t);
  }
  mark_moved(slot, ((int64_t)slot.generation << 32) | index);
}

void InstanceManager2D::update_instance(int64_t id, const Transform2D &transform) {
  int index = slot_of(id);
  if (index < 0) {
    print_error("update_instance: invalid or stale instance id ", id);
    return;
  }
  update_slot(index, transform);
}

void InstanceManager2D::update_instances(const PackedInt64Array &ids,
                                         const PackedFloat32Array &transforms) {
  ERR_FAIL_COND_MSG(transforms.size() != ids.size() * 8,
                    "update_instances: need 8 floats per id.");
  const int64_t *id = ids.ptr();
  const float *r = transforms.ptr();
  int invalid = 0;
  for (int64_t i = 0; i < ids.size(); i++, r += 8) {
    int index = slot_of(id[i]);
    if (index < 0) {
      invalid++;
      continue;
    }
    update_slot(index, Transform2D(r[0], r[4], r[1], r[5], r[3], r[7]));
  }
  if (invalid > 0) {
    print_error("update_instances: skipped invalid or stale ids: ", invalid);
  }
}

void InstanceManager2D::free_slot(int index) {
  Slot &slot = slots[index];
  slot.generation++; // invalidates outstanding ids
  slot.renderer = nullptr;
  slot.render_index = -1;
  free_slots.push_back(index);
}

void InstanceManager2D::remove_instance(int64_t id) {
  int index = slot_of(id);
  if (index < 0) {
    print_error("remove_instance: invalid or stale instance id ", id);
    return;
  }
  detach(slots[index]);
  free_slot(index);
}

void InstanceManager2D::reset_instance_interpolation(int64_t id) {
  int index = slot_of(id);
  if (index >= 0) {
    slots[index].renderer->reset_interpolation(slots[index].render_index);
  }
}

RID InstanceManager2D::acquire_quad(const Rect2 &rect) {
  Quad &quad = quads[{rect.position.x, rect.position.y, rect.size.x, rect.size.y}];
  if (quad.references++ == 0) {
    Vector2 a = rect.position;
    Vector2 b = rect.get_end();
    PackedVector2Array vertices;
    vertices.push_back(a);
    vertices.push_back(Vector2(b.x, a.y));
    vertices.push_back(b);
    vertices.push_back(Vector2(a.x, b.y));
    PackedVector2Array uvs;
    uvs.push_back(Vector2(0, 0));
    uvs.push_back(Vector2(1, 0));
    uvs.push_back(Vector2(1, 1));
    uvs.push_back(Vector2(0, 1));
    PackedInt32Array indices;
    for (int i : {0, 1, 2, 0, 2, 3}) {
      indices.push_back(i);
    }
    Array arrays;
    arrays.resize(RenderingServer::ARRAY_MAX);
    arrays[RenderingServer::ARRAY_VERTEX] = vertices;
    arrays[RenderingServer::ARRAY_TEX_UV] = uvs;
    arrays[RenderingServer::ARRAY_INDEX] = indices;
    RenderingServer *rs = RenderingServer::get_singleton();
    quad.mesh = rs->mesh_create();
    rs->mesh_add_surface_from_arrays(quad.mesh, RenderingServer::PRIMITIVE_TRIANGLES,
                                     arrays);
  }
  return quad.mesh;
}

void InstanceManager2D::release_quad(const Rect2 &rect) {
  auto it = quads.find({rect.position.x, rect.position.y, rect.size.x, rect.size.y});
  ERR_FAIL_COND(it == quads.end());
  if (--it->second.references == 0) {
    RenderingServer::get_singleton()->free_rid(it->second.mesh);
    quads.erase(it);
  }
}

RID InstanceManager2D::acquire_item() {
  live_items++;
  if (item_pool.empty()) {
    return RenderingServer::get_singleton()->canvas_item_create();
  }
  RID item = item_pool.back();
  item_pool.pop_back();
  return item;
}

void InstanceManager2D::release_item(const RID &item) {
  live_items--;
  RenderingServer *rs = RenderingServer::get_singleton();
  rs->canvas_item_clear(item);
  rs->canvas_item_set_parent(item, RID());
  item_pool.push_back(item);
}

void InstanceManager2D::forget_dirty(InstancedRenderer2D *renderer) {
  for (auto *list : {&dirty_frame, &dirty_physics}) {
    list->erase(std::remove(list->begin(), list->end(), renderer), list->end());
  }
#ifdef INSTANCED_2D_CANVAS_ECHO
  echoes.erase(std::remove(echoes.begin(), echoes.end(), renderer), echoes.end());
#endif
  forget_visible_counts(renderer);
}

// Rebuilds its batches; instances and their ids carry over.
void InstanceManager2D::on_definition_changed(uint64_t definition_id) {
  std::vector<std::unique_ptr<InstancedRenderer2D>> old;
  for (auto it = renderers.begin(); it != renderers.end();) {
    if ((uint64_t)it->second->get_definition()->get_instance_id() == definition_id) {
      forget_dirty(it->second.get());
      old.push_back(std::move(it->second));
      it = renderers.erase(it);
    } else {
      ++it;
    }
  }
  for (std::unique_ptr<InstancedRenderer2D> &r : old) {
    for (int i = 0; i < r->get_instance_count(); i++) {
      place(r->get_owner(i), *r->get_parent(), r->get_definition(),
            r->read_transform(i));
    }
  }
}

void InstanceManager2D::update_sleep() {
  if (sleep_watch.empty()) {
    return;
  }
  uint64_t now = Engine::get_singleton()->get_process_frames();
  size_t kept = 0;
  for (size_t i = 0; i < sleep_watch.size(); i++) {
    int64_t id = sleep_watch[i];
    int index = slot_of(id);
    if (index < 0) {
      continue;
    }
    Slot &slot = slots[index];
    if (slot.rest || sleep_after_frames <= 0 || slot.is_static ||
        slot.renderer->is_y_sorted()) {
      slot.sleep_watched = false;
    } else if (now - slot.last_moved_frame < (uint64_t)sleep_after_frames) {
      sleep_watch[kept++] = id;
    } else {
      // At rest there is nothing to interpolate, so switching batches can't pop.
      slot.sleep_watched = false;
      slot.rest = true;
      move(index, slot.renderer->read_transform(slot.render_index));
    }
  }
  sleep_watch.resize(kept);
}

// Y-sorted chunks only get canvas items while their bounds touch the view, so
// off-screen instances cost nothing in the canvas cull and sort.
void InstanceManager2D::update_views() {
  for (auto it = parents.begin(); it != parents.end();) {
    InstanceParent2D &parent = it->second;
    CanvasItem *ci = Object::cast_to<CanvasItem>(ObjectDB::get_instance(parent.id));
    if (!ci || parent.renderer_count == 0) {
      for (auto r = renderers.begin(); !ci && r != renderers.end();) {
        if (r->second->get_parent() != &parent) {
          ++r;
          continue;
        }
        forget_dirty(r->second.get());
        for (int i = 0; i < r->second->get_instance_count(); i++) {
          free_slot(r->second->get_owner(i));
        }
        r = renderers.erase(r);
      }
      if (parent.sort_root.is_valid()) {
        RenderingServer::get_singleton()->free_rid(parent.sort_root);
      }
      it = parents.erase(it);
      continue;
    }
    ++it;
    if (parent.y_sorted.empty() || !ci->is_inside_tree()) {
      continue;
    }
    // The editor pans and zooms through the global canvas transform.
    Transform2D to_view = ci->get_viewport()->get_global_canvas_transform() *
                          ci->get_global_transform_with_canvas();
    if (to_view.determinant() == 0) {
      continue;
    }
    Rect2 view = to_view.affine_inverse().xform(ci->get_viewport_rect());
    if (view == parent.view && !parent.view_dirty) {
      continue;
    }
    parent.view = view;
    parent.view_dirty = false;
    for (InstancedRenderer2D *r : parent.y_sorted) {
      r->set_on_screen(r->get_instance_count() > 0 && view.intersects(r->get_bounds()));
    }
  }
}

void InstanceManager2D::drop_empty_renderers() {
  if (!any_empty) {
    return;
  }
  any_empty = false;
  for (auto it = renderers.begin(); it != renderers.end();) {
    InstancedRenderer2D *r = it->second.get();
    if (r->get_instance_count() > 0) {
      ++it;
    } else if (r->is_flush_queued()) {
      any_empty = true; // the dirty list still points at it
      ++it;
    } else {
      forget_dirty(r);
      it = renderers.erase(it);
    }
  }
}

void InstanceManager2D::flush_frame() {
  for (const VisibleCount &c : counts_due) {
    c.renderer->set_visible_count(c.count);
  }
  counts_due.swap(counts_waiting);
  counts_waiting.clear();
  update_sleep();
  flush(false);
  drop_empty_renderers();
  update_views();
}

#ifdef INSTANCED_2D_CANVAS_ECHO
void InstanceManager2D::flush_echoes() {
  std::vector<InstancedRenderer2D *> batch;
  batch.swap(echoes);
  for (InstancedRenderer2D *r : batch) {
    r->echo_upload();
  }
}
#endif

// Deferred so it runs after _physics_process, still inside the tick.
void InstanceManager2D::queue_physics_flush() {
  if (!flush_queued && !dirty_physics.empty()) {
    flush_queued = true;
    callable_mp(this, &InstanceManager2D::flush_physics).call_deferred();
  }
}

void InstanceManager2D::queue_visible_count(InstancedRenderer2D *renderer, int count) {
  counts_waiting.push_back({renderer, count});
}

void InstanceManager2D::forget_visible_counts(InstancedRenderer2D *renderer) {
  for (auto *list : {&counts_waiting, &counts_due}) {
    list->erase(std::remove_if(list->begin(), list->end(),
                               [renderer](const VisibleCount &c) {
                                 return c.renderer == renderer;
                               }),
                list->end());
  }
}

void InstanceManager2D::flush_physics() {
  flush_queued = false;
  flush(true);
}

void InstanceManager2D::queue_flush(InstancedRenderer2D *renderer) {
  bool physics = renderer->is_interpolated();
  (physics ? dirty_physics : dirty_frame).push_back(renderer);
  // Changes outside a tick are picked up by the next physics_frame.
  if (physics && !flush_queued && Engine::get_singleton()->is_in_physics_frame()) {
    flush_queued = true;
    callable_mp(this, &InstanceManager2D::flush_physics).call_deferred();
  }
}

void InstanceManager2D::flush(bool p_interpolated) {
  std::vector<InstancedRenderer2D *> batch;
  batch.swap(p_interpolated ? dirty_physics : dirty_frame);
  for (InstancedRenderer2D *r : batch) {
    if (r->is_interpolated() != p_interpolated) {
      // Interpolation toggled while queued.
      (p_interpolated ? dirty_frame : dirty_physics).push_back(r);
      continue;
    }
    r->flush();
  }
}

} // namespace godot

#include "instance_manager_3d.h"
#include "godot_cpp/classes/camera3d.hpp"
#include "godot_cpp/classes/editor_interface.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/project_settings.hpp"
#include "godot_cpp/classes/rendering_server.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/classes/sub_viewport.hpp"
#include "godot_cpp/classes/window.hpp"
#include "godot_cpp/core/print_string.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "instance_settings.h"
#include <algorithm>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

InstanceManager3D *InstanceManager3D::singleton = nullptr;

void InstanceManager3D::_bind_methods() {
  ClassDB::bind_method(
      D_METHOD("add_instance", "world", "definition", "transform", "is_static"),
      &InstanceManager3D::add_instance, DEFVAL(false));
  ClassDB::bind_method(D_METHOD("update_instance", "id", "transform"),
                       &InstanceManager3D::update_instance);
  ClassDB::bind_method(D_METHOD("update_instances", "ids", "transforms"),
                       &InstanceManager3D::update_instances);
  ClassDB::bind_method(D_METHOD("update_instance_positions", "ids", "positions"),
                       &InstanceManager3D::update_instance_positions);
  ClassDB::bind_method(D_METHOD("update_instance_rotations", "ids", "rotations"),
                       &InstanceManager3D::update_instance_rotations);
  ClassDB::bind_method(D_METHOD("update_instances_packed", "ids", "transforms"),
                       &InstanceManager3D::update_instances_packed);
  ClassDB::bind_method(D_METHOD("remove_instance", "id"),
                       &InstanceManager3D::remove_instance);
  ClassDB::bind_method(D_METHOD("reset_instance_interpolation", "id"),
                       &InstanceManager3D::reset_instance_interpolation);
  ClassDB::bind_method(D_METHOD("is_instance_valid", "id"),
                       &InstanceManager3D::is_instance_valid);
  ClassDB::bind_method(D_METHOD("get_batch_count"),
                       &InstanceManager3D::get_batch_count);
}

void InstanceManager3D::register_project_settings() {
  instance_settings::add_setting(SETTING_DEFAULT_CHUNK_SIZE, 64.0,
                                 PROPERTY_HINT_RANGE,
                                 "0,1024,0.1,or_greater,suffix:m");
}

InstanceManager3D::InstanceManager3D() {
  singleton = this;
  load_settings();
  ProjectSettings::get_singleton()->connect(
      "settings_changed", callable_mp(this, &InstanceManager3D::load_settings));
}

InstanceManager3D::~InstanceManager3D() {
  ProjectSettings::get_singleton()->disconnect(
      "settings_changed", callable_mp(this, &InstanceManager3D::load_settings));
  // At extension shutdown the RenderingServer may already be gone (with our RIDs).
  if (Engine::get_singleton()->has_singleton("RenderingServer")) {
    renderers.clear();
  } else {
    for (auto &[key, renderer] : renderers) {
      renderer.release();
    }
  }
  singleton = nullptr;
}

void InstanceManager3D::load_settings() {
  ProjectSettings *ps = ProjectSettings::get_singleton();
  interpolation_mode = ps->get_setting(instance_settings::INTERPOLATION_MODE, 0);
  interpolation_quality =
      ps->get_setting(instance_settings::INTERPOLATION_QUALITY, 0);
  initial_capacity = ps->get_setting(instance_settings::INITIAL_CAPACITY, 16);
  sleep_after_frames =
      ps->get_setting(instance_settings::SLEEP_AFTER_FRAMES, 60);
  bool interpolate = instance_settings::should_interpolate(interpolation_mode);
  for (auto &[key, r] : renderers) {
    r->set_interpolation(interpolate, interpolation_quality);
  }
}

// Lazy: the RenderingServer and SceneTree don't exist yet at extension init.
void InstanceManager3D::connect_signals() {
  Callable on_draw = callable_mp(this, &InstanceManager3D::flush_frame);
  RenderingServer *rs = RenderingServer::get_singleton();
  if (!rs->is_connected("frame_pre_draw", on_draw)) {
    rs->connect("frame_pre_draw", on_draw);
  }
  SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
  Callable on_physics = callable_mp(this, &InstanceManager3D::queue_physics_flush);
  if (tree && !tree->is_connected("physics_frame", on_physics)) {
    tree->connect("physics_frame", on_physics);
  }
}

int InstanceManager3D::slot_of(int64_t id) const {
  int64_t slot = id & 0xFFFFFFFF;
  uint32_t generation = (uint32_t)(id >> 32);
  if (id < 0 || slot >= (int64_t)slots.size() || !slots[slot].renderer ||
      slots[slot].generation != generation) {
    return -1;
  }
  return (int)slot;
}

bool InstanceManager3D::is_instance_valid(int64_t id) const {
  return slot_of(id) >= 0;
}

static Vector3i cell_of(const Vector3 &origin, real_t chunk_size) {
  return chunk_size > 0 ? Vector3i((origin / chunk_size).floor()) : Vector3i();
}

InstancedRenderer3D *InstanceManager3D::get_renderer(
    const Ref<World3D> &world, const Ref<InstanceDefinition3D> &definition,
    const Vector3 &origin, bool rest) {
  Vector3i cell = cell_of(origin, definition->get_chunk_size());
  Key key{world->get_scenario().get_id(), definition->get_instance_id(),
          cell.x, cell.y, cell.z, rest};
  std::unique_ptr<InstancedRenderer3D> &r = renderers[key];
  if (!r) {
    r = std::make_unique<InstancedRenderer3D>(world, definition, cell, rest,
                                              initial_capacity);
    r->set_interpolation(instance_settings::should_interpolate(interpolation_mode),
                         interpolation_quality);
    if (r->has_lods()) {
      lod_renderers.push_back(r.get());
      lods_dirty = true;
    }
    Callable on_changed = callable_mp(this, &InstanceManager3D::on_definition_changed)
                              .bind(definition->get_instance_id());
    if (!definition->is_connected("changed", on_changed)) {
      definition->connect("changed", on_changed);
    }
  }
  return r.get();
}

int64_t InstanceManager3D::add_instance(
    const Ref<World3D> &world, const Ref<InstanceDefinition3D> &definition,
    const Transform3D &transform, bool is_static) {
  ERR_FAIL_COND_V(world.is_null(), -1);
  ERR_FAIL_COND_V(definition.is_null(), -1);
  connect_signals();

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
  place(index, world, definition, transform);
  int64_t id = ((int64_t)slot.generation << 32) | index;
  // Instances that never move fall asleep too.
  mark_moved(slot, id, Engine::get_singleton()->get_process_frames());
  return id;
}

template <typename T> static uint64_t id_of(const Ref<T> &resource) {
  return resource.is_valid() ? (uint64_t)resource->get_instance_id() : 0;
}

static InstanceDefinition3D *live_definition(uint64_t id) {
  return Object::cast_to<InstanceDefinition3D>(ObjectDB::get_instance(id));
}

// Nodes have no resource to share, so equal settings resolve to one definition.
// Held weakly: the batches using it keep it alive.
Ref<InstanceDefinition3D> InstanceManager3D::share_definition(
    const Ref<Mesh> &mesh, const Ref<Material> &material,
    const TypedArray<InstancedMeshLOD> &lods, int cast_shadow, uint32_t layers,
    real_t visibility_range_end, real_t chunk_size) {
  std::vector<uint64_t> lod_ids;
  for (int i = 0; i < lods.size(); i++) {
    lod_ids.push_back(id_of(Ref<InstancedMeshLOD>(lods[i])));
  }
  SharedKey key{id_of(mesh), id_of(material), lod_ids, cast_shadow,
                layers, visibility_range_end, chunk_size};
  auto found = shared_definitions.find(key);
  if (found != shared_definitions.end()) {
    if (InstanceDefinition3D *live = live_definition(found->second)) {
      return Ref<InstanceDefinition3D>(live);
    }
  }
  for (auto it = shared_definitions.begin(); it != shared_definitions.end();) {
    it = live_definition(it->second) ? std::next(it) : shared_definitions.erase(it);
  }
  Ref<InstanceDefinition3D> definition;
  definition.instantiate();
  definition->set_mesh(mesh);
  definition->set_material(material);
  definition->set_lods(lods);
  definition->set_cast_shadow(cast_shadow);
  definition->set_layers(layers);
  definition->set_visibility_range_end(visibility_range_end);
  definition->set_chunk_size(chunk_size);
  shared_definitions[key] = definition->get_instance_id();
  return definition;
}

void InstanceManager3D::detach(Slot &slot) {
  int moved = slot.renderer->remove(slot.render_index);
  if (moved >= 0) {
    slots[moved].render_index = slot.render_index;
  }
  any_empty = any_empty || slot.renderer->get_instance_count() == 0;
  slot.renderer = nullptr;
  slot.render_index = -1;
}

void InstanceManager3D::place(int index, const Ref<World3D> &world,
                              const Ref<InstanceDefinition3D> &definition,
                              const Transform3D &t) {
  Slot &slot = slots[index];
  slot.renderer = get_renderer(world, definition, t.origin, slot.rest);
  slot.render_index = slot.renderer->add(index, t);
}

void InstanceManager3D::move(int index, const Transform3D &t) {
  Slot &slot = slots[index];
  Ref<World3D> world = slot.renderer->get_world();
  Ref<InstanceDefinition3D> definition = slot.renderer->get_definition();
  detach(slot);
  place(index, world, definition, t);
}

// Waking from sleep or leaving the chunk changes batch.
bool InstanceManager3D::needs_move(const Slot &slot, const Vector3 &origin) const {
  return (slot.rest && !slot.is_static) || !slot.renderer->in_cell(origin);
}

void InstanceManager3D::mark_moved(Slot &slot, int64_t id, uint64_t now) {
  slot.last_moved_frame = now;
  if (!slot.sleep_watched && !slot.is_static && sleep_after_frames > 0) {
    slot.sleep_watched = true;
    sleep_watch.push_back(id);
  }
}

void InstanceManager3D::update_slot(int index, const Transform3D &t,
                                    uint64_t now) {
  Slot &slot = slots[index];
  if (needs_move(slot, t.origin)) {
    slot.rest = slot.is_static;
    move(index, t); // first tick snaps if interpolated
  } else {
    slot.renderer->set_transform(slot.render_index, t);
  }
  mark_moved(slot, ((int64_t)slot.generation << 32) | index, now);
}

void InstanceManager3D::update_instance(int64_t id, const Transform3D &transform) {
  int index = slot_of(id);
  if (index < 0) {
    print_error("update_instance: invalid or stale instance id ", id);
    return;
  }
  update_slot(index, transform, Engine::get_singleton()->get_process_frames());
}

void InstanceManager3D::update_instances(const PackedInt64Array &ids,
                                         const TypedArray<Transform3D> &transforms) {
  ERR_FAIL_COND_MSG(transforms.size() != ids.size(),
                    "update_instances: need one transform per id.");
  uint64_t now = Engine::get_singleton()->get_process_frames();
  const int64_t *id = ids.ptr();
  int invalid = 0;
  for (int64_t i = 0; i < ids.size(); i++) {
    int index = slot_of(id[i]);
    if (index < 0) {
      invalid++;
      continue;
    }
    update_slot(index, transforms[i], now);
  }
  if (invalid > 0) {
    print_error("update_instances: skipped invalid or stale ids: ", invalid);
  }
}

void InstanceManager3D::update_instance_positions(
    const PackedInt64Array &ids, const PackedVector3Array &positions) {
  ERR_FAIL_COND_MSG(positions.size() != ids.size(),
                    "update_instance_positions: need one position per id.");
  uint64_t now = Engine::get_singleton()->get_process_frames();
  const int64_t *id = ids.ptr();
  const Vector3 *pos = positions.ptr();
  int invalid = 0;
  for (int64_t i = 0; i < ids.size(); i++) {
    int index = slot_of(id[i]);
    if (index < 0) {
      invalid++;
      continue;
    }
    Slot &slot = slots[index];
    if (needs_move(slot, pos[i])) {
      Transform3D t = slot.renderer->read_transform(slot.render_index);
      t.origin = pos[i];
      update_slot(index, t, now);
    } else {
      slot.renderer->set_origin(slot.render_index, pos[i]);
      mark_moved(slot, id[i], now);
    }
  }
  if (invalid > 0) {
    print_error("update_instance_positions: skipped invalid or stale ids: ",
                invalid);
  }
}

void InstanceManager3D::update_instance_rotations(
    const PackedInt64Array &ids, const PackedVector3Array &rotations) {
  ERR_FAIL_COND_MSG(rotations.size() != ids.size(),
                    "update_instance_rotations: need one rotation per id.");
  uint64_t now = Engine::get_singleton()->get_process_frames();
  const int64_t *id = ids.ptr();
  const Vector3 *rot = rotations.ptr();
  int invalid = 0;
  for (int64_t i = 0; i < ids.size(); i++) {
    int index = slot_of(id[i]);
    if (index < 0) {
      invalid++;
      continue;
    }
    const Slot &slot = slots[index];
    Transform3D t = slot.renderer->read_transform(slot.render_index);
    t.basis.set_euler_scale(rot[i], t.basis.get_scale());
    update_slot(index, t, now);
  }
  if (invalid > 0) {
    print_error("update_instance_rotations: skipped invalid or stale ids: ",
                invalid);
  }
}

void InstanceManager3D::update_instances_packed(
    const PackedInt64Array &ids, const PackedFloat32Array &transforms) {
  ERR_FAIL_COND_MSG(transforms.size() != ids.size() * 12,
                    "update_instances_packed: need 12 floats per id.");
  uint64_t now = Engine::get_singleton()->get_process_frames();
  const int64_t *id = ids.ptr();
  const float *r = transforms.ptr();
  int invalid = 0;
  for (int64_t i = 0; i < ids.size(); i++, r += 12) {
    int index = slot_of(id[i]);
    if (index < 0) {
      invalid++;
      continue;
    }
    Slot &slot = slots[index];
    if (needs_move(slot, Vector3(r[3], r[7], r[11]))) {
      update_slot(index,
                  Transform3D(r[0], r[1], r[2], r[4], r[5], r[6], r[8], r[9],
                              r[10], r[3], r[7], r[11]),
                  now);
    } else {
      slot.renderer->set_packed(slot.render_index, r);
      mark_moved(slot, id[i], now);
    }
  }
  if (invalid > 0) {
    print_error("update_instances_packed: skipped invalid or stale ids: ",
                invalid);
  }
}

void InstanceManager3D::free_slot(int index) {
  Slot &slot = slots[index];
  slot.generation++; // invalidates outstanding ids
  slot.renderer = nullptr;
  slot.render_index = -1;
  free_slots.push_back(index);
}

void InstanceManager3D::remove_instance(int64_t id) {
  int index = slot_of(id);
  if (index < 0) {
    print_error("remove_instance: invalid or stale instance id ", id);
    return;
  }
  detach(slots[index]);
  free_slot(index);
}

void InstanceManager3D::reset_instance_interpolation(int64_t id) {
  int index = slot_of(id);
  if (index >= 0) {
    slots[index].renderer->reset_interpolation(slots[index].render_index);
  }
}

void InstanceManager3D::forget(InstancedRenderer3D *renderer) {
  for (auto *list : {&dirty_frame, &dirty_physics, &lod_renderers}) {
    list->erase(std::remove(list->begin(), list->end(), renderer), list->end());
  }
}

// Rebuilds its batches; instances and their ids carry over.
void InstanceManager3D::on_definition_changed(uint64_t definition_id) {
  std::vector<std::unique_ptr<InstancedRenderer3D>> old;
  for (auto it = renderers.begin(); it != renderers.end();) {
    if ((uint64_t)it->second->get_definition()->get_instance_id() == definition_id) {
      forget(it->second.get());
      old.push_back(std::move(it->second));
      it = renderers.erase(it);
    } else {
      ++it;
    }
  }
  for (std::unique_ptr<InstancedRenderer3D> &r : old) {
    for (int i = 0; i < r->get_instance_count(); i++) {
      place(r->get_owner(i), r->get_world(), r->get_definition(),
            r->read_transform(i));
    }
  }
}

void InstanceManager3D::update_sleep() {
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
    if (slot.rest || sleep_after_frames <= 0 || slot.is_static) {
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

// One camera picks the LOD for every viewport.
void InstanceManager3D::update_lods() {
  if (lod_renderers.empty()) {
    return;
  }
  Camera3D *camera = nullptr;
  if (Engine::get_singleton()->is_editor_hint()) {
    if (SubViewport *vp = EditorInterface::get_singleton()->get_editor_viewport_3d(0)) {
      camera = vp->get_camera_3d();
    }
  } else if (SceneTree *tree = Object::cast_to<SceneTree>(
                 Engine::get_singleton()->get_main_loop())) {
    camera = tree->get_root()->get_camera_3d();
  }
  if (!camera) {
    return;
  }
  Vector3 position = camera->get_global_position();
  if (position == last_camera_position && !lods_dirty) {
    return;
  }
  last_camera_position = position;
  lods_dirty = false;
  RID camera_scenario = camera->get_world_3d()->get_scenario();
  for (InstancedRenderer3D *r : lod_renderers) {
    if (r->get_scenario() == camera_scenario) {
      r->update_lod(position);
    }
  }
}

void InstanceManager3D::drop_empty_renderers() {
  if (!any_empty) {
    return;
  }
  any_empty = false;
  for (auto it = renderers.begin(); it != renderers.end();) {
    InstancedRenderer3D *r = it->second.get();
    if (r->get_instance_count() > 0) {
      ++it;
    } else if (r->is_flush_queued()) {
      any_empty = true; // the dirty list still points at it
      ++it;
    } else {
      forget(r);
      it = renderers.erase(it);
    }
  }
}

void InstanceManager3D::flush_frame() {
  update_sleep();
  flush(false);
  drop_empty_renderers();
  update_lods();
}

// Deferred so it runs after _physics_process, still inside the tick.
void InstanceManager3D::queue_physics_flush() {
  if (!flush_queued && !dirty_physics.empty()) {
    flush_queued = true;
    callable_mp(this, &InstanceManager3D::flush_physics).call_deferred();
  }
}

void InstanceManager3D::flush_physics() {
  flush_queued = false;
  flush(true);
}

void InstanceManager3D::queue_flush(InstancedRenderer3D *renderer) {
  bool physics = renderer->is_interpolated();
  (physics ? dirty_physics : dirty_frame).push_back(renderer);
  // Changes outside a tick are picked up by the next physics_frame.
  if (physics && !flush_queued && Engine::get_singleton()->is_in_physics_frame()) {
    flush_queued = true;
    callable_mp(this, &InstanceManager3D::flush_physics).call_deferred();
  }
}

void InstanceManager3D::flush(bool p_interpolated) {
  std::vector<InstancedRenderer3D *> batch;
  batch.swap(p_interpolated ? dirty_physics : dirty_frame);
  for (InstancedRenderer3D *r : batch) {
    if (r->is_interpolated() != p_interpolated) {
      // Interpolation toggled while queued.
      (p_interpolated ? dirty_frame : dirty_physics).push_back(r);
      continue;
    }
    r->flush();
    lods_dirty = lods_dirty || r->has_lods();
  }
}

} // namespace godot

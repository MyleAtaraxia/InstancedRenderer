#include "instance_manager.h"
#include <algorithm>
#include "godot_cpp/classes/camera3d.hpp"
#include "godot_cpp/classes/editor_interface.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/sub_viewport.hpp"
#include "godot_cpp/classes/window.hpp"
#include "godot_cpp/classes/project_settings.hpp"
#include "godot_cpp/classes/rendering_server.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "instanced_mesh_3d.h"
#include <godot_cpp/core/class_db.hpp>

namespace godot {

InstanceManager *InstanceManager::singleton = nullptr;

void InstanceManager::_bind_methods() {
  ClassDB::bind_method(
      D_METHOD("get_renderer", "world", "mesh", "material", "cell",
               "chunk_size", "cast_shadow", "layers", "is_static",
               "visibility_range_end", "sleeping", "lods"),
      &InstanceManager::get_renderer_bind, DEFVAL(Variant()),
      DEFVAL(Vector3i()), DEFVAL(0.0), DEFVAL(1), DEFVAL(1), DEFVAL(false),
      DEFVAL(0.0), DEFVAL(false), DEFVAL(TypedArray<InstancedMeshLOD>()));
  ClassDB::bind_static_method("InstanceManager",
                              D_METHOD("get_cell", "position", "chunk_size"),
                              &InstanceManager::get_cell);
}

void add_setting(const String &name, const Variant &default_value,
                 PropertyHint hint, const String &hint_string) {
  ProjectSettings *ps = ProjectSettings::get_singleton();
  if (!ps->has_setting(name)) {
    ps->set_setting(name, default_value);
  }
  ps->set_initial_value(name, default_value);
  ps->set_as_basic(name, true);
  Dictionary info;
  info["name"] = name;
  info["type"] = default_value.get_type();
  info["hint"] = hint;
  info["hint_string"] = hint_string;
  ps->add_property_info(info);
}

void InstanceManager::register_project_settings() {
  add_setting(SETTING_INTERPOLATION_MODE, INTERPOLATION_FOLLOW_PROJECT,
              PROPERTY_HINT_ENUM, "Follow Project,Disabled");
  add_setting(SETTING_INTERPOLATION_QUALITY, 0, PROPERTY_HINT_ENUM,
              "Fast,High");
  add_setting(SETTING_INITIAL_CAPACITY, 16, PROPERTY_HINT_RANGE,
              "1,65536,1,or_greater");
  add_setting(SETTING_DEFAULT_CHUNK_SIZE, 64.0, PROPERTY_HINT_RANGE,
              "0,1024,0.1,or_greater,suffix:m");
  add_setting(SETTING_SLEEP_AFTER_FRAMES, 60, PROPERTY_HINT_RANGE,
              "0,600,1,or_greater");
  add_setting(SETTING_GIZMO_COLOR, Color(1.0, 0.8, 0.2));
}

InstanceManager::InstanceManager() {
  singleton = this;
  load_settings();
  ProjectSettings::get_singleton()->connect(
      "settings_changed", callable_mp(this, &InstanceManager::load_settings));
}

void InstanceManager::load_settings() {
  ProjectSettings *ps = ProjectSettings::get_singleton();
  interpolation_mode = ps->get_setting(SETTING_INTERPOLATION_MODE, 0);
  interpolation_quality = ps->get_setting(SETTING_INTERPOLATION_QUALITY, 0);
  initial_capacity = ps->get_setting(SETTING_INITIAL_CAPACITY, 16);
  sleep_after_frames = ps->get_setting(SETTING_SLEEP_AFTER_FRAMES, 60);
  // Initial capacity only applies to renderers created from now on.
  bool interpolate = should_interpolate();
  for (auto &[key, r] : renderers) {
    r->set_interpolation(interpolate, interpolation_quality);
  }
}

bool InstanceManager::should_interpolate() const {
  if (Engine::get_singleton()->is_editor_hint()) {
    return false; // the editor never runs physics interpolation
  }
  if (interpolation_mode == INTERPOLATION_DISABLED) {
    return false;
  }
  SceneTree *tree =
      Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
  return tree && tree->is_physics_interpolation_enabled();
}

InstanceManager::~InstanceManager() {
  ProjectSettings::get_singleton()->disconnect(
      "settings_changed", callable_mp(this, &InstanceManager::load_settings));
  // At extension shutdown the RenderingServer may already be gone (with our RIDs).
  if (Engine::get_singleton()->has_singleton("RenderingServer")) {
    dirty_frame.clear();
    dirty_physics.clear();
    lod_renderers.clear();
    renderers.clear();
  }
  singleton = nullptr;
}

Vector3i InstanceManager::get_cell(const Vector3 &position,
                                   real_t chunk_size) {
  if (chunk_size <= 0) {
    return Vector3i(); // chunking off
  }
  return Vector3i((position / chunk_size).floor());
}

// Lazy: the RenderingServer and SceneTree don't exist yet at extension init.
void InstanceManager::connect_signals() {
  Callable on_draw = callable_mp(this, &InstanceManager::flush_frame);
  RenderingServer *rs = RenderingServer::get_singleton();
  if (!rs->is_connected("frame_pre_draw", on_draw)) {
    rs->connect("frame_pre_draw", on_draw);
  }
  SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
  Callable on_physics = callable_mp(this, &InstanceManager::queue_physics_flush);
  if (tree && !tree->is_connected("physics_frame", on_physics)) {
    tree->connect("physics_frame", on_physics);
  }
}

LodLevels InstanceManager::make_lod_levels(
    const Ref<Mesh> &mesh, const TypedArray<InstancedMeshLOD> &lods,
    real_t visibility_range_end) {
  LodLevels levels;
  for (int i = 0; i < lods.size(); i++) {
    Ref<InstancedMeshLOD> lod = lods[i];
    if (lod.is_valid() && lod->get_mesh().is_valid() && lod->get_distance() > 0 &&
        (visibility_range_end <= 0 ||
         lod->get_distance() < visibility_range_end)) {
      levels.push_back({lod->get_distance(), lod->get_mesh()});
    }
  }
  std::stable_sort(levels.begin(), levels.end(),
                   [](const auto &a, const auto &b) { return a.first < b.first; });
  levels.insert(levels.begin(), {real_t(0), mesh});
  return levels;
}

Ref<InstancedRenderer3D> InstanceManager::get_renderer_bind(
    const Ref<World3D> &world, const Ref<Mesh> &mesh,
    const Ref<Material> &material, const Vector3i &cell, real_t chunk_size,
    int cast_shadow, uint32_t layers, bool is_static,
    real_t visibility_range_end, bool sleeping,
    const TypedArray<InstancedMeshLOD> &lods) {
  return get_renderer(world, make_lod_levels(mesh, lods, visibility_range_end),
                      material, cell, chunk_size, cast_shadow, layers,
                      is_static, visibility_range_end, sleeping);
}

Ref<InstancedRenderer3D>
InstanceManager::get_renderer(const Ref<World3D> &world, const LodLevels &lods,
                              const Ref<Material> &material,
                              const Vector3i &cell, real_t chunk_size,
                              int cast_shadow, uint32_t layers, bool is_static,
                              real_t visibility_range_end, bool sleeping) {
  ERR_FAIL_COND_V(world.is_null(), Ref<InstancedRenderer3D>());
  connect_signals();
  RID scenario = world->get_scenario();
  const Ref<Mesh> &mesh = lods[0].second;
  std::vector<std::pair<real_t, int64_t>> lod_key;
  for (size_t i = 1; i < lods.size(); i++) {
    lod_key.push_back({lods[i].first, lods[i].second->get_rid().get_id()});
  }
  Key key{scenario.get_id(),
          mesh.is_valid() ? mesh->get_rid().get_id() : 0,
          material.is_valid() ? material->get_rid().get_id() : 0,
          cast_shadow, layers, chunk_size,
          is_static, sleeping, visibility_range_end, lod_key,
          cell.x, cell.y, cell.z};

  Ref<InstancedRenderer3D> &r = renderers[key];
  if (r.is_null()) {
    r.instantiate();
    // Apart from static batches, so mass wake-ups don't re-upload scenery.
    r->setup(scenario, lods, material, cast_shadow, layers, is_static || sleeping,
             visibility_range_end, initial_capacity);
    if (r->has_lods()) {
      lod_renderers.push_back(r.ptr());
      lods_dirty = true;
    }
    r->set_interpolation(should_interpolate(), interpolation_quality);
  }
  return r;
}

void InstanceManager::update_sleep() {
  if (sleep_watch.empty()) {
    return;
  }
  uint64_t now = Engine::get_singleton()->get_process_frames();
  size_t kept = 0;
  for (uint64_t id : sleep_watch) {
    InstancedMesh3D *node =
        Object::cast_to<InstancedMesh3D>(ObjectDB::get_instance(id));
    if (node && !node->update_sleep(now)) {
      sleep_watch[kept++] = id;
    }
  }
  sleep_watch.resize(kept);
}

// One camera picks the LOD for every viewport.
void InstanceManager::update_lods() {
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

void InstanceManager::flush_frame() {
  update_sleep();
  flush(false);
  update_lods();
}

// Deferred so it runs after _physics_process, still inside the tick.
void InstanceManager::queue_physics_flush() {
  if (!flush_queued && !dirty_physics.empty()) {
    flush_queued = true;
    callable_mp(this, &InstanceManager::flush_physics).call_deferred();
  }
}

void InstanceManager::flush_physics() {
  flush_queued = false;
  flush(true);
}

void InstanceManager::queue_flush(InstancedRenderer3D *renderer) {
  bool physics = renderer->is_interpolated();
  (physics ? dirty_physics : dirty_frame)
      .push_back(Ref<InstancedRenderer3D>(renderer));
  // Changes outside a tick are picked up by the next physics_frame.
  if (physics && !flush_queued && Engine::get_singleton()->is_in_physics_frame()) {
    flush_queued = true;
    callable_mp(this, &InstanceManager::flush_physics).call_deferred();
  }
}

void InstanceManager::flush(bool p_interpolated) {
  std::vector<Ref<InstancedRenderer3D>> batch;
  batch.swap(p_interpolated ? dirty_physics : dirty_frame);
  bool any_empty = false;
  for (Ref<InstancedRenderer3D> &r : batch) {
    if (r->is_interpolated() != p_interpolated) {
      // Interpolation toggled while queued.
      (p_interpolated ? dirty_frame : dirty_physics).push_back(r);
      continue;
    }
    r->flush();
    lods_dirty = lods_dirty || r->has_lods();
    any_empty = any_empty || r->get_instance_count() == 0;
  }
  batch.clear();
  if (!any_empty) {
    return;
  }
  // Drop empty renderers only the map still holds.
  for (auto it = renderers.begin(); it != renderers.end();) {
    Ref<InstancedRenderer3D> &r = it->second;
    if (r->get_reference_count() == 1 && r->get_instance_count() == 0) {
      lod_renderers.erase(std::remove(lod_renderers.begin(), lod_renderers.end(), r.ptr()),
                          lod_renderers.end());
      it = renderers.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace godot

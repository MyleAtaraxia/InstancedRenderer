#include "instanced_bake_2d.h"
#include "instance_manager_2d.h"
#include "instanced_node_2d.h"
#include <godot_cpp/classes/editor_export_preset.hpp>
#include <godot_cpp/classes/editor_file_system.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <map>
#include <vector>

namespace godot {

static const char *PROGRESS_SCRIPT =
    "res://addons/instanced_renderer/GDProgress/gd_progress.gd";
static const char *PROGRESS_TASK = "InstancedRenderer";

void InstancedBake2D::_bind_methods() {
  ClassDB::bind_static_method("InstancedBake2D",
                              D_METHOD("bake_scene", "scene", "progress", "from", "to"),
                              &InstancedBake2D::bake_scene,
                              DEFVAL(Ref<Script>()), DEFVAL(0.0), DEFVAL(1.0));
  ClassDB::bind_method(D_METHOD("set_baked", "baked"),
                       &InstancedBake2D::set_baked);
  ClassDB::bind_method(D_METHOD("get_baked"), &InstancedBake2D::get_baked);
  ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "baked", PROPERTY_HINT_NONE, "",
                            PROPERTY_USAGE_STORAGE),
               "set_baked", "get_baked");
}

void InstancedBake2D::_notification(int p_what) {
  if (p_what != NOTIFICATION_READY) {
    return;
  }
  InstanceManager2D *manager = InstanceManager2D::get_singleton();
  CanvasItem *parent = Object::cast_to<CanvasItem>(get_parent());
  ERR_FAIL_NULL(parent);
  for (int i = 0; i + 1 < baked.size(); i += 2) {
    Ref<InstanceDefinition2D> definition = baked[i];
    ERR_CONTINUE(definition.is_null());
    PackedVector2Array columns = baked[i + 1];
    const Vector2 *c = columns.ptr();
    for (int64_t j = 0; j + 2 < columns.size(); j += 3) {
      manager->add_instance(parent, definition,
                            Transform2D(c[j], c[j + 1], c[j + 2]), true);
    }
  }
  queue_free();
}

Ref<Script> InstancedBake2D::progress_script() {
  ResourceLoader *loader = ResourceLoader::get_singleton();
  return loader->exists(PROGRESS_SCRIPT) ? Ref<Script>(loader->load(PROGRESS_SCRIPT))
                                         : Ref<Script>();
}

void InstancedBake2D::set_progress_task(bool p_added) {
  Ref<Script> progress = progress_script();
  if (progress.is_valid()) {
    progress->call(p_added ? "add_task" : "remove_task", PROGRESS_TASK);
  }
}

int InstancedBake2D::bake_scene(Node *p_scene, const Ref<Script> &p_progress,
                                double p_from, double p_to) {
  // Starts at now so small scenes finish without opening the window.
  const uint64_t started = Time::get_singleton()->get_ticks_msec();
  uint64_t reported = started;
  String status = vformat("Baking %s", p_scene->get_name());
  auto report = [&](double p_ratio) {
    uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (p_progress.is_valid() && now - reported >= 33) {
      reported = now;
      p_progress->call("update", PROGRESS_TASK, status,
                       p_from + (p_to - p_from) * p_ratio);
    }
  };
  struct Batch {
    Ref<InstanceDefinition2D> definition;
    PackedVector2Array columns;
  };
  std::map<Node *, std::map<uint64_t, Batch>> parents;
  std::vector<Node *> done;
  TypedArray<Node> nodes =
      p_scene->find_children("*", "InstancedNode2D", true, false);
  for (int i = 0; i < nodes.size(); i++) {
    report(0.5 * i / nodes.size());
    InstancedNode2D *node = Object::cast_to<InstancedNode2D>(nodes[i]);
    // Groups and unique names mean something looks this node up.
    if (!node || node->get_owner() != p_scene ||
        !node->get_scene_file_path().is_empty() || !node->get_is_static() ||
        !node->is_visible() || node->get_child_count(true) > 0 ||
        node->get_script() != Variant() || !node->get_groups().is_empty() ||
        node->is_unique_name_in_owner() ||
        !Object::cast_to<CanvasItem>(node->get_parent())) {
      continue;
    }
    Ref<InstanceDefinition2D> definition = node->resolve_definition();
    if (definition.is_null()) {
      continue;
    }
    Transform2D t = node->get_transform();
    Batch &batch = parents[node->get_parent()][definition->get_instance_id()];
    batch.definition = definition;
    batch.columns.push_back(t.columns[0]);
    batch.columns.push_back(t.columns[1]);
    batch.columns.push_back(t.columns[2]);
    done.push_back(node);
  }
  // Back to front: Node keeps its child cache valid that way.
  for (auto it = done.rbegin(); it != done.rend(); ++it) {
    report(0.5 + 0.5 * (it - done.rbegin()) / done.size());
    (*it)->get_parent()->remove_child(*it);
    memdelete(*it);
  }
  for (auto &[parent, batches] : parents) {
    Array baked;
    for (auto &[id, batch] : batches) {
      baked.push_back(batch.definition);
      baked.push_back(batch.columns);
    }
    InstancedBake2D *bake = memnew(InstancedBake2D);
    bake->set_baked(baked);
    bake->set_name("InstancedBake2D");
    parent->add_child(bake);
    bake->set_owner(p_scene);
  }
  // Godot saves the scene once the plugin returns, and nothing reports from there.
  if (reported != started) {
    status = vformat("Saving %s", p_scene->get_name());
    reported = 0;
    report(1.0);
  }
  return (int)done.size();
}

bool InstancedBakeExportPlugin::_begin_customize_scenes(
    const Ref<EditorExportPlatform> &p_platform,
    const PackedStringArray &p_features) const {
  return ProjectSettings::get_singleton()->get_setting(
      InstanceManager2D::SETTING_BAKE_STATIC, true);
}

void InstancedBakeExportPlugin::add_scene_bytes(EditorFileSystemDirectory *p_directory) {
  Ref<EditorExportPreset> preset = get_export_preset();
  bool all = preset->get_export_filter() == EditorExportPreset::EXPORT_ALL_RESOURCES;
  for (int i = 0; i < p_directory->get_file_count(); i++) {
    String path = p_directory->get_file_path(i);
    if (p_directory->get_file_type(i) == StringName("PackedScene") &&
        (all || preset->has_export_file(path))) {
      Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
      scene_bytes += file.is_valid() ? file->get_length() : 0;
    }
  }
  for (int i = 0; i < p_directory->get_subdir_count(); i++) {
    add_scene_bytes(p_directory->get_subdir(i));
  }
}

void InstancedBakeExportPlugin::_export_begin(const PackedStringArray &p_features,
                                              bool p_is_debug, const String &p_path,
                                              uint32_t p_flags) {
  scene_bytes = 0;
  scene_bytes_done = 0;
  if (!ProjectSettings::get_singleton()->get_setting(
          InstanceManager2D::SETTING_BAKE_STATIC, true)) {
    return;
  }
  progress = InstancedBake2D::progress_script();
  if (progress.is_valid()) {
    add_scene_bytes(EditorInterface::get_singleton()->get_resource_filesystem()->get_filesystem());
  }
}

void InstancedBakeExportPlugin::end_progress() {
  if (progress.is_valid()) {
    progress->call("finish", PROGRESS_TASK);
    progress.unref();
  }
}

Node *InstancedBakeExportPlugin::_customize_scene(Node *p_scene,
                                                  const String &p_path) {
  Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
  uint64_t bytes = file.is_valid() ? file->get_length() : 0;
  scene_bytes = MAX(scene_bytes, scene_bytes_done + bytes); // a filter can hide dependencies
  uint64_t done = scene_bytes_done;
  scene_bytes_done += bytes;
  // An inherited scene loads its base's bakes too, so baking here would double them.
  Ref<PackedScene> packed = ResourceLoader::get_singleton()->load(p_path);
  if (packed.is_valid() && packed->get_state()->get_node_instance(0).is_valid()) {
    return nullptr;
  }
  double total = MAX(scene_bytes, (uint64_t)1);
  // The rest of the scene's share is Godot's save, which is what the bar waits on there.
  int count = InstancedBake2D::bake_scene(p_scene, progress, done / total,
                                          (done + 0.8 * bytes) / total);
  return count > 0 ? p_scene : nullptr;
}

} // namespace godot

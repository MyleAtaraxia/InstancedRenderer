#include "instanced_bake_2d.h"
#include "instance_manager_2d.h"
#include "instanced_node_2d.h"
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <map>
#include <vector>

namespace godot {

void InstancedBake2D::_bind_methods() {
  ClassDB::bind_static_method("InstancedBake2D", D_METHOD("bake_scene", "scene"),
                              &InstancedBake2D::bake_scene);
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

int InstancedBake2D::bake_scene(Node *p_scene) {
  struct Batch {
    Ref<InstanceDefinition2D> definition;
    PackedVector2Array columns;
  };
  std::map<Node *, std::map<uint64_t, Batch>> parents;
  std::vector<Node *> done;
  TypedArray<Node> nodes =
      p_scene->find_children("*", "InstancedNode2D", true, false);
  for (int i = 0; i < nodes.size(); i++) {
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
  return (int)done.size();
}

bool InstancedBakeExportPlugin::_begin_customize_scenes(
    const Ref<EditorExportPlatform> &p_platform,
    const PackedStringArray &p_features) const {
  return ProjectSettings::get_singleton()->get_setting(
      InstanceManager2D::SETTING_BAKE_STATIC, true);
}

Node *InstancedBakeExportPlugin::_customize_scene(Node *p_scene,
                                                  const String &p_path) {
  // An inherited scene loads its base's bakes too, so baking here would double them.
  Ref<PackedScene> packed = ResourceLoader::get_singleton()->load(p_path);
  if (packed.is_valid() && packed->get_state()->get_node_instance(0).is_valid()) {
    return nullptr;
  }
  return InstancedBake2D::bake_scene(p_scene) > 0 ? p_scene : nullptr;
}

} // namespace godot

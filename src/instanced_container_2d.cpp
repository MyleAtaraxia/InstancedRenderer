#include "instanced_container_2d.h"
#include "instance_manager_2d.h"
#include "instanced_node_2d.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <map>
#include <vector>

namespace godot {

void InstancedContainer2D::_bind_methods() {
  ClassDB::bind_method(D_METHOD("bake"), &InstancedContainer2D::bake);
  ClassDB::bind_method(D_METHOD("set_baked", "baked"),
                       &InstancedContainer2D::set_baked);
  ClassDB::bind_method(D_METHOD("get_baked"), &InstancedContainer2D::get_baked);
  ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "baked", PROPERTY_HINT_NONE, "",
                            PROPERTY_USAGE_STORAGE),
               "set_baked", "get_baked");
}

void InstancedContainer2D::_notification(int p_what) {
  if (p_what != NOTIFICATION_READY) {
    return;
  }
  InstanceManager2D *manager = InstanceManager2D::get_singleton();
  for (int i = 0; i + 1 < baked.size(); i += 2) {
    Ref<InstanceDefinition2D> definition = baked[i];
    ERR_CONTINUE(definition.is_null());
    PackedVector2Array columns = baked[i + 1];
    const Vector2 *c = columns.ptr();
    for (int64_t j = 0; j + 2 < columns.size(); j += 3) {
      manager->add_instance(this, definition,
                            Transform2D(c[j], c[j + 1], c[j + 2]), true);
    }
  }
  baked = Array(); // the manager has its own copy now
}

int InstancedContainer2D::bake() {
  struct Batch {
    Ref<InstanceDefinition2D> definition;
    PackedVector2Array columns;
  };
  std::map<uint64_t, Batch> batches;
  std::vector<Node *> done;
  for (int i = 0; i < get_child_count(); i++) {
    InstancedNode2D *node = Object::cast_to<InstancedNode2D>(get_child(i));
    if (!node || !node->get_is_static() || !node->is_visible() ||
        node->get_child_count(true) > 0 || node->get_script() != Variant()) {
      continue;
    }
    Ref<InstanceDefinition2D> definition = node->resolve_definition();
    if (definition.is_null()) {
      continue;
    }
    Transform2D t = node->get_transform();
    Batch &batch = batches[definition->get_instance_id()];
    batch.definition = definition;
    batch.columns.push_back(t.columns[0]);
    batch.columns.push_back(t.columns[1]);
    batch.columns.push_back(t.columns[2]);
    done.push_back(node);
  }
  for (auto &[id, batch] : batches) {
    baked.push_back(batch.definition);
    baked.push_back(batch.columns);
  }
  // Back to front: Node keeps its child cache valid that way.
  for (auto it = done.rbegin(); it != done.rend(); ++it) {
    remove_child(*it);
    memdelete(*it);
  }
  return (int)done.size();
}

Node *InstancedContainerExportPlugin::_customize_scene(Node *p_scene,
                                                       const String &p_path) {
  TypedArray<Node> containers =
      p_scene->find_children("*", "InstancedContainer2D", true, false);
  containers.push_back(p_scene);
  int count = 0;
  for (int i = 0; i < containers.size(); i++) {
    InstancedContainer2D *container =
        Object::cast_to<InstancedContainer2D>(containers[i]);
    if (container) {
      count += container->bake();
    }
  }
  return count > 0 ? p_scene : nullptr;
}

} // namespace godot

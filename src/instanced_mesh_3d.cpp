#include "instanced_mesh_3d.h"
#include "instance_manager_3d.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

// Nodes left at the default follow the project setting; set ones are saved.
InstancedMesh3D::InstancedMesh3D() {
  chunk_size = ProjectSettings::get_singleton()->get_setting(
      InstanceManager3D::SETTING_DEFAULT_CHUNK_SIZE, 64.0);
}

void InstancedMesh3D::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_mesh", "mesh"),
                       &InstancedMesh3D::set_mesh);
  ClassDB::bind_method(D_METHOD("get_mesh"), &InstancedMesh3D::get_mesh);
  ClassDB::bind_method(D_METHOD("set_material", "material"),
                       &InstancedMesh3D::set_material);
  ClassDB::bind_method(D_METHOD("get_material"),
                       &InstancedMesh3D::get_material);
  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh",
                            PROPERTY_HINT_RESOURCE_TYPE, "Mesh"),
               "set_mesh", "get_mesh");
  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material",
                            PROPERTY_HINT_RESOURCE_TYPE,
                            "BaseMaterial3D,ShaderMaterial"),
               "set_material", "get_material");

  ClassDB::bind_method(D_METHOD("set_is_static", "is_static"),
                       &InstancedMesh3D::set_is_static);
  ClassDB::bind_method(D_METHOD("get_is_static"),
                       &InstancedMesh3D::get_is_static);
  ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_static"), "set_is_static",
               "get_is_static");

  ClassDB::bind_method(D_METHOD("set_chunk_size", "size"),
                       &InstancedMesh3D::set_chunk_size);
  ClassDB::bind_method(D_METHOD("get_chunk_size"),
                       &InstancedMesh3D::get_chunk_size);
  ClassDB::bind_method(D_METHOD("set_cast_shadow", "setting"),
                       &InstancedMesh3D::set_cast_shadow);
  ClassDB::bind_method(D_METHOD("get_cast_shadow"),
                       &InstancedMesh3D::get_cast_shadow);
  ClassDB::bind_method(D_METHOD("set_layers", "layers"),
                       &InstancedMesh3D::set_layers);
  ClassDB::bind_method(D_METHOD("get_layers"), &InstancedMesh3D::get_layers);
  ADD_GROUP("Rendering", "");
  ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "chunk_size", PROPERTY_HINT_RANGE,
                            "0,1024,0.1,or_greater,suffix:m"),
               "set_chunk_size", "get_chunk_size");
  ADD_PROPERTY(PropertyInfo(Variant::INT, "cast_shadow", PROPERTY_HINT_ENUM,
                            "Off,On,Double-Sided,Shadows Only"),
               "set_cast_shadow", "get_cast_shadow");
  ADD_PROPERTY(PropertyInfo(Variant::INT, "layers",
                            PROPERTY_HINT_LAYERS_3D_RENDER),
               "set_layers", "get_layers");
  ClassDB::bind_method(D_METHOD("set_visibility_range_end", "distance"),
                       &InstancedMesh3D::set_visibility_range_end);
  ClassDB::bind_method(D_METHOD("get_visibility_range_end"),
                       &InstancedMesh3D::get_visibility_range_end);
  ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "visibility_range_end",
                            PROPERTY_HINT_RANGE,
                            "0,4096,0.1,or_greater,suffix:m"),
               "set_visibility_range_end", "get_visibility_range_end");

  ClassDB::bind_method(D_METHOD("set_lods", "lods"), &InstancedMesh3D::set_lods);
  ClassDB::bind_method(D_METHOD("get_lods"), &InstancedMesh3D::get_lods);
  ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "lods", PROPERTY_HINT_ARRAY_TYPE,
                            vformat("%d/%d:InstancedMeshLOD", Variant::OBJECT,
                                    PROPERTY_HINT_RESOURCE_TYPE)),
               "set_lods", "get_lods");
}

void InstancedMesh3D::_notification(int p_what) {
  switch (p_what) {
  case NOTIFICATION_ENTER_TREE:
    set_notify_transform(true);
    register_instance();
    break;
  case NOTIFICATION_READY:
    // Kept in the editor so the scene stays editable.
    if (is_static && !Engine::get_singleton()->is_editor_hint()) {
      instance_id = -1; // the instance outlives the node
      queue_free();
    }
    break;
  case NOTIFICATION_TRANSFORM_CHANGED:
    if (instance_id >= 0) {
      InstanceManager3D::get_singleton()->update_instance(instance_id,
                                                          get_global_transform());
    }
    break;
  case NOTIFICATION_RESET_PHYSICS_INTERPOLATION:
    if (instance_id >= 0) {
      InstanceManager3D::get_singleton()->reset_instance_interpolation(instance_id);
    }
    break;
  case NOTIFICATION_VISIBILITY_CHANGED:
    reregister();
    break;
  case NOTIFICATION_EXIT_TREE:
    unregister_instance();
    break;
  }
}

void InstancedMesh3D::register_instance() {
  if (instance_id >= 0 || mesh.is_null() || !is_visible_in_tree()) {
    return;
  }
  InstanceManager3D *manager = InstanceManager3D::get_singleton();
  Ref<InstanceDefinition3D> definition = manager->share_definition(
      mesh, material, lods, cast_shadow, layers, visibility_range_end,
      chunk_size);
  instance_id = manager->add_instance(get_world_3d(), definition,
                                      get_global_transform(), is_static);
}

void InstancedMesh3D::unregister_instance() {
  if (instance_id >= 0) {
    InstanceManager3D::get_singleton()->remove_instance(instance_id);
  }
  instance_id = -1;
}

void InstancedMesh3D::reregister() {
  if (is_inside_tree()) {
    unregister_instance();
    register_instance();
  }
}

void InstancedMesh3D::set_mesh(const Ref<Mesh> &p_mesh) {
  mesh = p_mesh;
  reregister();
  update_gizmos();
}
Ref<Mesh> InstancedMesh3D::get_mesh() const { return mesh; }

void InstancedMesh3D::set_material(const Ref<Material> &p_material) {
  material = p_material;
  reregister();
}
Ref<Material> InstancedMesh3D::get_material() const { return material; }

void InstancedMesh3D::set_is_static(bool p_static) {
  is_static = p_static;
  reregister();
}
bool InstancedMesh3D::get_is_static() const { return is_static; }

void InstancedMesh3D::set_chunk_size(real_t p_size) {
  chunk_size = p_size;
  reregister();
}
real_t InstancedMesh3D::get_chunk_size() const { return chunk_size; }

void InstancedMesh3D::set_cast_shadow(int p_setting) {
  cast_shadow = p_setting;
  reregister();
}
int InstancedMesh3D::get_cast_shadow() const { return cast_shadow; }

void InstancedMesh3D::set_layers(uint32_t p_layers) {
  layers = p_layers;
  reregister();
}
uint32_t InstancedMesh3D::get_layers() const { return layers; }

void InstancedMesh3D::set_visibility_range_end(real_t p_distance) {
  visibility_range_end = p_distance;
  reregister();
}
real_t InstancedMesh3D::get_visibility_range_end() const {
  return visibility_range_end;
}

void InstancedMesh3D::set_lods(const TypedArray<InstancedMeshLOD> &p_lods) {
  lods = p_lods;
  reregister();
}
TypedArray<InstancedMeshLOD> InstancedMesh3D::get_lods() const { return lods; }

} // namespace godot

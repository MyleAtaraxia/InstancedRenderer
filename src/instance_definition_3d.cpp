#include "instance_definition_3d.h"
#include "instance_manager_3d.h"
#include <algorithm>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

namespace godot {

// Definitions left at the default follow the project setting; set ones are saved.
InstanceDefinition3D::InstanceDefinition3D() {
  chunk_size = ProjectSettings::get_singleton()->get_setting(
      InstanceManager3D::SETTING_DEFAULT_CHUNK_SIZE, 64.0);
}

void InstanceDefinition3D::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_mesh", "mesh"),
                       &InstanceDefinition3D::set_mesh);
  ClassDB::bind_method(D_METHOD("get_mesh"), &InstanceDefinition3D::get_mesh);
  ClassDB::bind_method(D_METHOD("set_material", "material"),
                       &InstanceDefinition3D::set_material);
  ClassDB::bind_method(D_METHOD("get_material"),
                       &InstanceDefinition3D::get_material);
  ClassDB::bind_method(D_METHOD("set_lods", "lods"),
                       &InstanceDefinition3D::set_lods);
  ClassDB::bind_method(D_METHOD("get_lods"), &InstanceDefinition3D::get_lods);
  ClassDB::bind_method(D_METHOD("set_cast_shadow", "setting"),
                       &InstanceDefinition3D::set_cast_shadow);
  ClassDB::bind_method(D_METHOD("get_cast_shadow"),
                       &InstanceDefinition3D::get_cast_shadow);
  ClassDB::bind_method(D_METHOD("set_layers", "layers"),
                       &InstanceDefinition3D::set_layers);
  ClassDB::bind_method(D_METHOD("get_layers"),
                       &InstanceDefinition3D::get_layers);
  ClassDB::bind_method(D_METHOD("set_visibility_range_end", "distance"),
                       &InstanceDefinition3D::set_visibility_range_end);
  ClassDB::bind_method(D_METHOD("get_visibility_range_end"),
                       &InstanceDefinition3D::get_visibility_range_end);
  ClassDB::bind_method(D_METHOD("set_chunk_size", "size"),
                       &InstanceDefinition3D::set_chunk_size);
  ClassDB::bind_method(D_METHOD("get_chunk_size"),
                       &InstanceDefinition3D::get_chunk_size);

  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh",
                            PROPERTY_HINT_RESOURCE_TYPE, "Mesh"),
               "set_mesh", "get_mesh");
  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material",
                            PROPERTY_HINT_RESOURCE_TYPE,
                            "BaseMaterial3D,ShaderMaterial"),
               "set_material", "get_material");
  ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "lods", PROPERTY_HINT_ARRAY_TYPE,
                            vformat("%d/%d:InstancedMeshLOD", Variant::OBJECT,
                                    PROPERTY_HINT_RESOURCE_TYPE)),
               "set_lods", "get_lods");
  ADD_PROPERTY(PropertyInfo(Variant::INT, "cast_shadow", PROPERTY_HINT_ENUM,
                            "Off,On,Double-Sided,Shadows Only"),
               "set_cast_shadow", "get_cast_shadow");
  ADD_PROPERTY(PropertyInfo(Variant::INT, "layers",
                            PROPERTY_HINT_LAYERS_3D_RENDER),
               "set_layers", "get_layers");
  ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "visibility_range_end",
                            PROPERTY_HINT_RANGE,
                            "0,4096,0.1,or_greater,suffix:m"),
               "set_visibility_range_end", "get_visibility_range_end");
  ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "chunk_size", PROPERTY_HINT_RANGE,
                            "0,1024,0.1,or_greater,suffix:m"),
               "set_chunk_size", "get_chunk_size");
}

void InstanceDefinition3D::on_resource_changed() { emit_changed(); }

// Editing a mesh or a LOD level in place only emits `changed` on it.
void InstanceDefinition3D::watch(const Ref<Resource> &p_old,
                                 const Ref<Resource> &p_new) {
  Callable on_changed =
      callable_mp(this, &InstanceDefinition3D::on_resource_changed);
  if (p_old.is_valid() && p_old->is_connected("changed", on_changed)) {
    p_old->disconnect("changed", on_changed);
  }
  if (p_new.is_valid() && !p_new->is_connected("changed", on_changed)) {
    p_new->connect("changed", on_changed);
  }
}

void InstanceDefinition3D::set_mesh(const Ref<Mesh> &p_mesh) {
  watch(mesh, p_mesh);
  mesh = p_mesh;
  emit_changed();
}

void InstanceDefinition3D::set_material(const Ref<Material> &p_material) {
  material = p_material;
  emit_changed();
}

void InstanceDefinition3D::set_lods(const TypedArray<InstancedMeshLOD> &p_lods) {
  for (int i = 0; i < lods.size(); i++) {
    watch(Ref<Resource>(lods[i]), Ref<Resource>());
  }
  lods = p_lods;
  for (int i = 0; i < lods.size(); i++) {
    watch(Ref<Resource>(), Ref<Resource>(lods[i]));
  }
  emit_changed();
}

void InstanceDefinition3D::set_cast_shadow(int p_setting) {
  cast_shadow = p_setting;
  emit_changed();
}

void InstanceDefinition3D::set_layers(uint32_t p_layers) {
  layers = p_layers;
  emit_changed();
}

void InstanceDefinition3D::set_visibility_range_end(real_t p_distance) {
  visibility_range_end = p_distance;
  emit_changed();
}

void InstanceDefinition3D::set_chunk_size(real_t p_size) {
  chunk_size = p_size;
  emit_changed();
}

LodLevels InstanceDefinition3D::get_lod_levels() const {
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

} // namespace godot

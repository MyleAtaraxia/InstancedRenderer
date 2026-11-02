#include "instance_definition_2d.h"
#include "instance_manager_2d.h"
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

namespace godot {

// Definitions left at the default follow the project setting; set ones are saved.
InstanceDefinition2D::InstanceDefinition2D() {
  chunk_size = ProjectSettings::get_singleton()->get_setting(
      InstanceManager2D::SETTING_DEFAULT_CHUNK_SIZE, 256.0);
}

void InstanceDefinition2D::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_texture", "texture"),
                       &InstanceDefinition2D::set_texture);
  ClassDB::bind_method(D_METHOD("get_texture"),
                       &InstanceDefinition2D::get_texture);
  ClassDB::bind_method(D_METHOD("set_mesh", "mesh"),
                       &InstanceDefinition2D::set_mesh);
  ClassDB::bind_method(D_METHOD("get_mesh"), &InstanceDefinition2D::get_mesh);
  ClassDB::bind_method(D_METHOD("set_material", "material"),
                       &InstanceDefinition2D::set_material);
  ClassDB::bind_method(D_METHOD("get_material"),
                       &InstanceDefinition2D::get_material);
  ClassDB::bind_method(D_METHOD("set_offset", "offset"),
                       &InstanceDefinition2D::set_offset);
  ClassDB::bind_method(D_METHOD("get_offset"),
                       &InstanceDefinition2D::get_offset);
  ClassDB::bind_method(D_METHOD("set_modulate", "modulate"),
                       &InstanceDefinition2D::set_modulate);
  ClassDB::bind_method(D_METHOD("get_modulate"),
                       &InstanceDefinition2D::get_modulate);
  ClassDB::bind_method(D_METHOD("set_y_sorted", "y_sorted"),
                       &InstanceDefinition2D::set_y_sorted);
  ClassDB::bind_method(D_METHOD("is_y_sorted"),
                       &InstanceDefinition2D::is_y_sorted);
  ClassDB::bind_method(D_METHOD("set_z_index", "z_index"),
                       &InstanceDefinition2D::set_z_index);
  ClassDB::bind_method(D_METHOD("get_z_index"),
                       &InstanceDefinition2D::get_z_index);
  ClassDB::bind_method(D_METHOD("set_chunk_size", "size"),
                       &InstanceDefinition2D::set_chunk_size);
  ClassDB::bind_method(D_METHOD("get_chunk_size"),
                       &InstanceDefinition2D::get_chunk_size);
  ClassDB::bind_method(D_METHOD("set_pixels_per_meter", "pixels"),
                       &InstanceDefinition2D::set_pixels_per_meter);
  ClassDB::bind_method(D_METHOD("get_pixels_per_meter"),
                       &InstanceDefinition2D::get_pixels_per_meter);
  ClassDB::bind_method(D_METHOD("set_texture_filter", "filter"),
                       &InstanceDefinition2D::set_texture_filter);
  ClassDB::bind_method(D_METHOD("get_texture_filter"),
                       &InstanceDefinition2D::get_texture_filter);
  ClassDB::bind_method(D_METHOD("set_texture_repeat", "repeat"),
                       &InstanceDefinition2D::set_texture_repeat);
  ClassDB::bind_method(D_METHOD("get_texture_repeat"),
                       &InstanceDefinition2D::get_texture_repeat);

  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture",
                            PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"),
               "set_texture", "get_texture");
  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh",
                            PROPERTY_HINT_RESOURCE_TYPE, "Mesh"),
               "set_mesh", "get_mesh");
  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material",
                            PROPERTY_HINT_RESOURCE_TYPE,
                            "CanvasItemMaterial,ShaderMaterial"),
               "set_material", "get_material");
  ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset", PROPERTY_HINT_NONE,
                            "suffix:px"),
               "set_offset", "get_offset");
  ADD_PROPERTY(PropertyInfo(Variant::COLOR, "modulate"), "set_modulate",
               "get_modulate");
  ADD_PROPERTY(PropertyInfo(Variant::BOOL, "y_sorted"), "set_y_sorted",
               "is_y_sorted");
  ADD_PROPERTY(PropertyInfo(Variant::INT, "z_index", PROPERTY_HINT_RANGE,
                            "-4096,4096,1"),
               "set_z_index", "get_z_index");
  ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "chunk_size", PROPERTY_HINT_RANGE,
                            "0,4096,1,or_greater,suffix:px"),
               "set_chunk_size", "get_chunk_size");
  ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pixels_per_meter",
                            PROPERTY_HINT_RANGE,
                            "0.001,1000,0.001,or_greater,suffix:px/m"),
               "set_pixels_per_meter", "get_pixels_per_meter");
  ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_filter", PROPERTY_HINT_ENUM,
                            "Default,Nearest,Linear,Nearest Mipmap,Linear Mipmap,"
                            "Nearest Mipmap Anisotropic,Linear Mipmap Anisotropic"),
               "set_texture_filter", "get_texture_filter");
  ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_repeat", PROPERTY_HINT_ENUM,
                            "Default,Disabled,Enabled,Mirror"),
               "set_texture_repeat", "get_texture_repeat");
}

void InstanceDefinition2D::on_resource_changed() { emit_changed(); }

// A reimported texture can change size, which changes the quad.
void InstanceDefinition2D::watch(const Ref<Resource> &p_old,
                                 const Ref<Resource> &p_new) {
  Callable on_changed =
      callable_mp(this, &InstanceDefinition2D::on_resource_changed);
  if (p_old.is_valid() && p_old->is_connected("changed", on_changed)) {
    p_old->disconnect("changed", on_changed);
  }
  if (p_new.is_valid() && !p_new->is_connected("changed", on_changed)) {
    p_new->connect("changed", on_changed);
  }
}

void InstanceDefinition2D::set_texture(const Ref<Texture2D> &p_texture) {
  watch(texture, p_texture);
  texture = p_texture;
  emit_changed();
}

void InstanceDefinition2D::set_mesh(const Ref<Mesh> &p_mesh) {
  watch(mesh, p_mesh);
  mesh = p_mesh;
  emit_changed();
}

void InstanceDefinition2D::set_material(const Ref<Material> &p_material) {
  material = p_material;
  emit_changed();
}

void InstanceDefinition2D::set_offset(const Vector2 &p_offset) {
  offset = p_offset;
  emit_changed();
}

void InstanceDefinition2D::set_modulate(const Color &p_modulate) {
  modulate = p_modulate;
  emit_changed();
}

void InstanceDefinition2D::set_y_sorted(bool p_y_sorted) {
  y_sorted = p_y_sorted;
  emit_changed();
}

void InstanceDefinition2D::set_z_index(int p_z_index) {
  z_index = p_z_index;
  emit_changed();
}

void InstanceDefinition2D::set_chunk_size(real_t p_size) {
  chunk_size = p_size;
  emit_changed();
}

void InstanceDefinition2D::set_pixels_per_meter(real_t p_pixels) {
  pixels_per_meter = MAX(p_pixels, (real_t)0.001);
  emit_changed();
}

void InstanceDefinition2D::set_texture_filter(int p_filter) {
  texture_filter = p_filter;
  emit_changed();
}

void InstanceDefinition2D::set_texture_repeat(int p_repeat) {
  texture_repeat = p_repeat;
  emit_changed();
}

Rect2 InstanceDefinition2D::get_local_rect() const {
  if (mesh.is_valid()) {
    AABB box = mesh->get_aabb();
    return Rect2(box.position.x * pixels_per_meter,
                 box.position.y * pixels_per_meter,
                 box.size.x * pixels_per_meter, box.size.y * pixels_per_meter);
  }
  if (texture.is_valid()) {
    Vector2 size = texture->get_size();
    return Rect2(offset - size / 2, size);
  }
  return Rect2();
}

} // namespace godot

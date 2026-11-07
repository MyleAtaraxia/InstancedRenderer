#include "instanced_node_2d.h"
#include "instance_manager_2d.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh_instance2d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

InstancedNode2D::InstancedNode2D() {
  chunk_size = ProjectSettings::get_singleton()->get_setting(
      InstanceManager2D::SETTING_DEFAULT_CHUNK_SIZE, 512.0);
}

void InstancedNode2D::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_y_sorted", "y_sorted"),
                       &InstancedNode2D::set_y_sorted);
  ClassDB::bind_method(D_METHOD("is_y_sorted"), &InstancedNode2D::is_y_sorted);
  ClassDB::bind_method(D_METHOD("set_is_static", "is_static"),
                       &InstancedNode2D::set_is_static);
  ClassDB::bind_method(D_METHOD("get_is_static"),
                       &InstancedNode2D::get_is_static);
  ClassDB::bind_method(D_METHOD("set_chunk_size", "size"),
                       &InstancedNode2D::set_chunk_size);
  ClassDB::bind_method(D_METHOD("get_chunk_size"),
                       &InstancedNode2D::get_chunk_size);
  ClassDB::bind_method(D_METHOD("set_instance_material", "material"),
                       &InstancedNode2D::set_instance_material);
  ClassDB::bind_method(D_METHOD("get_instance_material"),
                       &InstancedNode2D::get_instance_material);
  ClassDB::bind_method(D_METHOD("set_instance_modulate", "modulate"),
                       &InstancedNode2D::set_instance_modulate);
  ClassDB::bind_method(D_METHOD("get_instance_modulate"),
                       &InstancedNode2D::get_instance_modulate);
  ClassDB::bind_method(D_METHOD("set_instance_z_index", "z_index"),
                       &InstancedNode2D::set_instance_z_index);
  ClassDB::bind_method(D_METHOD("get_instance_z_index"),
                       &InstancedNode2D::get_instance_z_index);
  ADD_PROPERTY(PropertyInfo(Variant::BOOL, "y_sorted"), "set_y_sorted",
               "is_y_sorted");
  ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_static"), "set_is_static",
               "get_is_static");
  ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "chunk_size", PROPERTY_HINT_RANGE,
                            "0,4096,1,or_greater,suffix:px"),
               "set_chunk_size", "get_chunk_size");
  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "instance_material",
                            PROPERTY_HINT_RESOURCE_TYPE,
                            "CanvasItemMaterial,ShaderMaterial"),
               "set_instance_material", "get_instance_material");
  ADD_PROPERTY(PropertyInfo(Variant::COLOR, "instance_modulate"),
               "set_instance_modulate", "get_instance_modulate");
  ADD_PROPERTY(PropertyInfo(Variant::INT, "instance_z_index",
                            PROPERTY_HINT_RANGE, "-4096,4096,1"),
               "set_instance_z_index", "get_instance_z_index");
}

// CanvasItem's own versions don't reach the batches, so they stay out of sight.
void InstancedNode2D::_validate_property(PropertyInfo &p_property) const {
  static const char *hidden[] = {"material", "use_parent_material", "modulate",
                                 "self_modulate", "z_index", "z_as_relative"};
  for (const char *name : hidden) {
    if (p_property.name == StringName(name)) {
      p_property.usage &= ~PROPERTY_USAGE_EDITOR;
    }
  }
}

void InstancedNode2D::_notification(int p_what) {
  InstanceManager2D *manager = InstanceManager2D::get_singleton();
  switch (p_what) {
  case NOTIFICATION_ENTER_TREE:
    set_notify_local_transform(true);
    register_instance();
    break;
  case NOTIFICATION_ENTER_CANVAS:
    canvas_attached = true; // the engine just parented it
    update_canvas_attachment();
    break;
  case NOTIFICATION_CHILD_ORDER_CHANGED:
    update_canvas_attachment();
    break;
  case NOTIFICATION_READY:
    update_picker();
    // Kept in the editor so the scene stays editable.
    if (is_static && !Engine::get_singleton()->is_editor_hint()) {
      instance_id = -1;
      queue_free();
    }
    break;
  case NOTIFICATION_LOCAL_TRANSFORM_CHANGED:
    if (instance_id >= 0) {
      manager->update_instance(instance_id, get_transform());
    }
    break;
  case NOTIFICATION_RESET_PHYSICS_INTERPOLATION:
    if (instance_id >= 0) {
      manager->reset_instance_interpolation(instance_id);
    }
    break;
  case NOTIFICATION_VISIBILITY_CHANGED:
    // Hidden parents already hide their children's canvas items.
    if (is_visible() != (instance_id >= 0)) {
      reregister();
    }
    break;
  case NOTIFICATION_DRAW:
    // The only sign of a texture filter or repeat change, own or inherited.
    if (instance_id >= 0 && (resolve_texture_filter() != used_filter ||
                             resolve_texture_repeat() != used_repeat)) {
      reregister();
    }
    break;
  case NOTIFICATION_EXIT_TREE:
    unregister_instance();
    break;
  }
}

void InstancedNode2D::register_instance() {
  CanvasItem *parent = Object::cast_to<CanvasItem>(get_parent());
  // VISIBILITY_CHANGED arrives before ENTER_TREE, so this can run twice.
  if (instance_id >= 0 || !parent || !is_visible()) {
    return;
  }
  Ref<InstanceDefinition2D> definition = resolve_definition();
  if (definition.is_valid()) {
    used_filter = definition->get_texture_filter();
    used_repeat = definition->get_texture_repeat();
    instance_id = InstanceManager2D::get_singleton()->add_instance(
        parent, definition, get_transform(), is_static);
  }
}

void InstancedNode2D::unregister_instance() {
  if (instance_id >= 0) {
    InstanceManager2D::get_singleton()->remove_instance(instance_id);
  }
  instance_id = -1;
}

void InstancedNode2D::reregister() {
  if (is_inside_tree()) {
    unregister_instance();
    register_instance();
  }
}

// The canvas editor only picks real drawables, and _edit_* can't be overridden
// from an extension. This invisible child resolves to its owner, this node, and
// isn't saved because the scene doesn't own it.
void InstancedNode2D::update_picker() {
  if (!Engine::get_singleton()->is_editor_hint() || !is_node_ready()) {
    return;
  }
  if (picker) {
    remove_child(picker);
    memdelete(picker);
    picker = nullptr;
  }
  if (resolve_definition().is_null() ||
      !Object::cast_to<CanvasItem>(get_parent())) {
    return;
  }
  picker = make_picker();
  picker->set_self_modulate(Color(1, 1, 1, 0));
  add_child(picker, false, INTERNAL_MODE_BACK);
  picker->set_owner(this);
}

// Unlike a Node3D, every Node2D has a server item, and the canvas cull walks each
// one even when it's empty. Instances draw under the parent's item, so this one
// only has to stay in the tree to carry children.
void InstancedNode2D::update_canvas_attachment() {
  CanvasItem *parent = Object::cast_to<CanvasItem>(get_parent());
  if (!is_inside_tree() || !parent || is_set_as_top_level() ||
      Engine::get_singleton()->is_editor_hint()) {
    return;
  }
  bool attach = get_child_count(true) > 0;
  if (attach != canvas_attached) {
    canvas_attached = attach;
    RenderingServer::get_singleton()->canvas_item_set_parent(
        get_canvas_item(), attach ? parent->get_canvas_item() : RID());
  }
}

// CanvasItem's *_in_tree getters aren't exposed. Its enums match the
// RenderingServer's, with "parent node" where the server has "default".
int InstancedNode2D::resolve_texture_filter() const {
  const CanvasItem *item = this;
  while (item) {
    if (item->get_texture_filter() != CanvasItem::TEXTURE_FILTER_PARENT_NODE) {
      return item->get_texture_filter();
    }
    item = item->is_set_as_top_level()
               ? nullptr
               : Object::cast_to<CanvasItem>(item->get_parent());
  }
  return 0;
}

int InstancedNode2D::resolve_texture_repeat() const {
  const CanvasItem *item = this;
  while (item) {
    if (item->get_texture_repeat() != CanvasItem::TEXTURE_REPEAT_PARENT_NODE) {
      return item->get_texture_repeat();
    }
    item = item->is_set_as_top_level()
               ? nullptr
               : Object::cast_to<CanvasItem>(item->get_parent());
  }
  return 0;
}

void InstancedNode2D::on_changed() {
  reregister();
  update_picker();
  update_configuration_warnings();
}

Ref<InstanceDefinition2D>
InstancedNode2D::share_definition(const Ref<Mesh> &p_mesh,
                                  const Ref<Texture2D> &p_texture,
                                  const Vector2 &p_offset,
                                  real_t p_pixels_per_meter) const {
  return InstanceManager2D::get_singleton()->share_definition(
      p_mesh, p_texture, instance_material, p_offset, instance_modulate,
      y_sorted, instance_z_index, chunk_size, p_pixels_per_meter,
      resolve_texture_filter(), resolve_texture_repeat());
}

void InstancedNode2D::set_instance_material(const Ref<Material> &p_material) {
  instance_material = p_material;
  on_changed();
}

void InstancedNode2D::set_instance_modulate(const Color &p_modulate) {
  instance_modulate = p_modulate;
  on_changed();
}

void InstancedNode2D::set_instance_z_index(int p_z_index) {
  instance_z_index = p_z_index;
  on_changed();
}

void InstancedNode2D::set_y_sorted(bool p_y_sorted) {
  y_sorted = p_y_sorted;
  on_changed();
}

void InstancedNode2D::set_is_static(bool p_static) {
  is_static = p_static;
  reregister();
}

void InstancedNode2D::set_chunk_size(real_t p_size) {
  chunk_size = p_size;
  on_changed();
}

PackedStringArray InstancedNode2D::_get_configuration_warnings() const {
  PackedStringArray warnings;
  if (!Object::cast_to<CanvasItem>(get_parent())) {
    warnings.push_back("Needs a CanvasItem parent; instances are drawn under it.");
  }
  if (is_set_as_top_level()) {
    warnings.push_back("top_level is ignored: instances stay relative to the parent.");
  }
  if (resolve_definition().is_null()) {
    warnings.push_back(get_missing_warning());
  }
  return warnings;
}

void InstancedSprite2D::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_texture", "texture"),
                       &InstancedSprite2D::set_texture);
  ClassDB::bind_method(D_METHOD("get_texture"), &InstancedSprite2D::get_texture);
  ClassDB::bind_method(D_METHOD("set_offset", "offset"),
                       &InstancedSprite2D::set_offset);
  ClassDB::bind_method(D_METHOD("get_offset"), &InstancedSprite2D::get_offset);
  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture",
                            PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"),
               "set_texture", "get_texture");
  ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset", PROPERTY_HINT_NONE,
                            "suffix:px"),
               "set_offset", "get_offset");
}

Ref<InstanceDefinition2D> InstancedSprite2D::resolve_definition() const {
  if (texture.is_null()) {
    return Ref<InstanceDefinition2D>();
  }
  return share_definition(Ref<Mesh>(), texture, offset);
}

void InstancedSprite2D::set_texture(const Ref<Texture2D> &p_texture) {
  texture = p_texture;
  on_changed();
}

void InstancedSprite2D::set_offset(const Vector2 &p_offset) {
  offset = p_offset;
  on_changed();
}

String InstancedSprite2D::get_missing_warning() const {
  return "Needs a texture.";
}

Node2D *InstancedSprite2D::make_picker() const {
  Sprite2D *sprite = memnew(Sprite2D);
  sprite->set_texture(texture);
  sprite->set_offset(offset);
  return sprite;
}

void InstancedMesh2D::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_mesh", "mesh"), &InstancedMesh2D::set_mesh);
  ClassDB::bind_method(D_METHOD("get_mesh"), &InstancedMesh2D::get_mesh);
  ClassDB::bind_method(D_METHOD("set_pixels_per_meter", "pixels"),
                       &InstancedMesh2D::set_pixels_per_meter);
  ClassDB::bind_method(D_METHOD("get_pixels_per_meter"),
                       &InstancedMesh2D::get_pixels_per_meter);
  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh",
                            PROPERTY_HINT_RESOURCE_TYPE, "Mesh"),
               "set_mesh", "get_mesh");
  ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pixels_per_meter",
                            PROPERTY_HINT_RANGE,
                            "0.001,1000,0.001,or_greater,suffix:px/m"),
               "set_pixels_per_meter", "get_pixels_per_meter");
}

Ref<InstanceDefinition2D> InstancedMesh2D::resolve_definition() const {
  if (mesh.is_null()) {
    return Ref<InstanceDefinition2D>();
  }
  return share_definition(mesh, Ref<Texture2D>(), Vector2(), pixels_per_meter);
}

void InstancedMesh2D::set_mesh(const Ref<Mesh> &p_mesh) {
  mesh = p_mesh;
  on_changed();
}

void InstancedMesh2D::set_pixels_per_meter(real_t p_pixels) {
  pixels_per_meter = MAX(p_pixels, (real_t)0.001);
  on_changed();
}

String InstancedMesh2D::get_missing_warning() const {
  return "Needs a mesh.";
}

Node2D *InstancedMesh2D::make_picker() const {
  MeshInstance2D *instance = memnew(MeshInstance2D);
  instance->set_mesh(mesh);
  instance->set_scale(Vector2(pixels_per_meter, pixels_per_meter));
  return instance;
}

} // namespace godot

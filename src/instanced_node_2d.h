#pragma once

#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/variant/packed_string_array.hpp"
#include "godot_cpp/classes/mesh.hpp"
#include "godot_cpp/classes/texture2d.hpp"
#include "instance_definition_2d.h"

namespace godot {

// Registers one instance with InstanceManager2D, relative to its parent.
class InstancedNode2D : public Node2D {
  GDCLASS(InstancedNode2D, Node2D)

  Ref<Material> instance_material;
  Color instance_modulate = Color(1, 1, 1, 1);
  int instance_z_index = 0;
  bool y_sorted = false;
  bool is_static = false;
  real_t chunk_size;
  int64_t instance_id = -1;
  Node2D *picker = nullptr; // editor only
  int used_filter = 0;
  int used_repeat = 0;

  void register_instance();
  void unregister_instance();
  void reregister();
  void update_picker();
  int resolve_texture_filter() const;
  int resolve_texture_repeat() const;

protected:
  static void _bind_methods();
  void _notification(int p_what);
  void _validate_property(PropertyInfo &p_property) const;
  virtual String get_missing_warning() const { return String(); }
  virtual Node2D *make_picker() const { return nullptr; }
  Ref<InstanceDefinition2D> share_definition(const Ref<Mesh> &p_mesh,
                                             const Ref<Texture2D> &p_texture,
                                             const Vector2 &p_offset,
                                             real_t p_pixels_per_meter = 1.0) const;
  void on_changed();

public:
  InstancedNode2D();
  // Equal nodes resolve to one definition, which is what lets them batch.
  virtual Ref<InstanceDefinition2D> resolve_definition() const {
    return Ref<InstanceDefinition2D>();
  }
  void set_instance_material(const Ref<Material> &p_material);
  Ref<Material> get_instance_material() const { return instance_material; }
  void set_instance_modulate(const Color &p_modulate);
  Color get_instance_modulate() const { return instance_modulate; }
  void set_instance_z_index(int p_z_index);
  int get_instance_z_index() const { return instance_z_index; }
  void set_y_sorted(bool p_y_sorted);
  bool is_y_sorted() const { return y_sorted; }
  void set_is_static(bool p_static);
  bool get_is_static() const { return is_static; }
  void set_chunk_size(real_t p_size);
  real_t get_chunk_size() const { return chunk_size; }
  PackedStringArray _get_configuration_warnings() const override;
};

class InstancedSprite2D : public InstancedNode2D {
  GDCLASS(InstancedSprite2D, InstancedNode2D)

  Ref<Texture2D> texture;
  Vector2 offset;

protected:
  static void _bind_methods();
  String get_missing_warning() const override;
  Node2D *make_picker() const override;

public:
  Ref<InstanceDefinition2D> resolve_definition() const override;
  void set_texture(const Ref<Texture2D> &p_texture);
  Ref<Texture2D> get_texture() const { return texture; }
  void set_offset(const Vector2 &p_offset);
  Vector2 get_offset() const { return offset; }
};

class InstancedMesh2D : public InstancedNode2D {
  GDCLASS(InstancedMesh2D, InstancedNode2D)

  Ref<Mesh> mesh;
  real_t pixels_per_meter = 1.0;

protected:
  static void _bind_methods();
  String get_missing_warning() const override;
  Node2D *make_picker() const override;

public:
  Ref<InstanceDefinition2D> resolve_definition() const override;
  void set_mesh(const Ref<Mesh> &p_mesh);
  Ref<Mesh> get_mesh() const { return mesh; }
  void set_pixels_per_meter(real_t p_pixels);
  real_t get_pixels_per_meter() const { return pixels_per_meter; }
};

} // namespace godot

#pragma once

#include "godot_cpp/classes/node3d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "instance_definition_3d.h"

namespace godot {

// One instance drawn through InstanceManager3D; equal nodes share a batch.
class InstancedMesh3D : public Node3D {
  GDCLASS(InstancedMesh3D, Node3D)

  Ref<Mesh> mesh;
  Ref<Material> material;
  bool is_static = false;
  real_t chunk_size = 64.0;
  int cast_shadow = 1; // RenderingServer::ShadowCastingSetting
  uint32_t layers = 1;
  real_t visibility_range_end = 0; // 0 = always drawn
  TypedArray<InstancedMeshLOD> lods;
  int64_t instance_id = -1;

  void register_instance();
  void unregister_instance();
  void reregister();

protected:
  static void _bind_methods();
  void _notification(int p_what);

public:
  InstancedMesh3D();

  void set_mesh(const Ref<Mesh> &p_mesh);
  Ref<Mesh> get_mesh() const;
  void set_material(const Ref<Material> &p_material);
  Ref<Material> get_material() const;
  void set_is_static(bool p_static);
  bool get_is_static() const;
  void set_chunk_size(real_t p_size);
  real_t get_chunk_size() const;
  void set_cast_shadow(int p_setting);
  int get_cast_shadow() const;
  void set_layers(uint32_t p_layers);
  uint32_t get_layers() const;
  void set_visibility_range_end(real_t p_distance);
  real_t get_visibility_range_end() const;
  void set_lods(const TypedArray<InstancedMeshLOD> &p_lods);
  TypedArray<InstancedMeshLOD> get_lods() const;
};

} // namespace godot

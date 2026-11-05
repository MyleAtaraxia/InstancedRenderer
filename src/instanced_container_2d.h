#pragma once

#include "godot_cpp/classes/editor_export_platform.hpp"
#include "godot_cpp/classes/editor_export_plugin.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/variant/array.hpp"

namespace godot {

// Holds static instances as data, so the nodes that authored them never load.
class InstancedContainer2D : public Node2D {
  GDCLASS(InstancedContainer2D, Node2D)

  Array baked; // definition, then its transforms as 3 Vector2 columns each

protected:
  static void _bind_methods();
  void _notification(int p_what);

public:
  // Moves plain static children into `baked`; returns how many.
  int bake();
  void set_baked(const Array &p_baked) { baked = p_baked; }
  Array get_baked() const { return baked; }
};

class InstancedContainerExportPlugin : public EditorExportPlugin {
  GDCLASS(InstancedContainerExportPlugin, EditorExportPlugin)

protected:
  static void _bind_methods() {}

public:
  String _get_name() const override { return "InstancedContainer2D"; }
  bool _begin_customize_scenes(const Ref<EditorExportPlatform> &p_platform,
                               const PackedStringArray &p_features) const override {
    return true;
  }
  uint64_t _get_customization_configuration_hash() const override { return 1; }
  Node *_customize_scene(Node *p_scene, const String &p_path) override;
};

} // namespace godot

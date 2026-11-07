#pragma once

#include "godot_cpp/classes/control.hpp"
#include "godot_cpp/classes/editor_plugin.hpp"
#include "instanced_bake_2d.h"

namespace godot {

// Godot has no 2D gizmo plugin; this outlines selected instance nodes.
class InstancedNode2DEditorPlugin : public EditorPlugin {
  GDCLASS(InstancedNode2DEditorPlugin, EditorPlugin)

  Ref<InstancedBakeExportPlugin> export_plugin;

protected:
  static void _bind_methods() {}
  void _notification(int p_what);

public:
  void _forward_canvas_force_draw_over_viewport(Control *p_viewport_control) override;
};

} // namespace godot

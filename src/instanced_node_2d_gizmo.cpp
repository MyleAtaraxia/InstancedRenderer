#include "instanced_node_2d_gizmo.h"
#include "godot_cpp/classes/editor_interface.hpp"
#include "godot_cpp/classes/editor_selection.hpp"
#include "godot_cpp/classes/project_settings.hpp"
#include "instance_manager.h"
#include "instanced_node_2d.h"

namespace godot {

void InstancedNode2DEditorPlugin::_notification(int p_what) {
  if (p_what == NOTIFICATION_ENTER_TREE) {
    set_force_draw_over_forwarding_enabled();
    export_plugin.instantiate();
    add_export_plugin(export_plugin);
  } else if (p_what == NOTIFICATION_EXIT_TREE) {
    remove_export_plugin(export_plugin);
    export_plugin.unref();
  }
}

void InstancedNode2DEditorPlugin::_forward_canvas_force_draw_over_viewport(
    Control *p_viewport_control) {
  Color color = ProjectSettings::get_singleton()->get_setting(
      InstanceManager::SETTING_GIZMO_COLOR, Color(1.0, 0.8, 0.2));
  TypedArray<Node> nodes =
      EditorInterface::get_singleton()->get_selection()->get_selected_nodes();
  for (int i = 0; i < nodes.size(); i++) {
    InstancedNode2D *node = Object::cast_to<InstancedNode2D>(nodes[i]);
    Ref<InstanceDefinition2D> definition =
        node ? node->resolve_definition() : Ref<InstanceDefinition2D>();
    if (definition.is_null()) {
      continue;
    }
    Rect2 rect = definition->get_local_rect();
    Transform2D xform = node->get_viewport_transform() * node->get_global_transform();
    PackedVector2Array points;
    points.push_back(xform.xform(rect.position));
    points.push_back(xform.xform(Vector2(rect.get_end().x, rect.position.y)));
    points.push_back(xform.xform(rect.get_end()));
    points.push_back(xform.xform(Vector2(rect.position.x, rect.get_end().y)));
    points.push_back(points[0]);
    p_viewport_control->draw_polyline(points, color);
  }
}

} // namespace godot

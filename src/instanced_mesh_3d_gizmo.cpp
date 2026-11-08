#include "instanced_mesh_3d_gizmo.h"
#include "godot_cpp/classes/standard_material3d.hpp"
#include "godot_cpp/classes/triangle_mesh.hpp"
#include "godot_cpp/classes/editor_interface.hpp"
#include "godot_cpp/classes/project_settings.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "instance_settings.h"
#include "instanced_mesh_3d.h"

namespace godot {

InstancedMesh3DGizmoPlugin::InstancedMesh3DGizmoPlugin() { load_color(); }

void InstancedMesh3DGizmoPlugin::load_color() {
  // Recreating a material under the same name replaces it.
  create_material("bounds", ProjectSettings::get_singleton()->get_setting(
                                instance_settings::GIZMO_COLOR,
                                Color(1.0, 0.8, 0.2)));
}

String InstancedMesh3DGizmoPlugin::_get_gizmo_name() const {
  return "InstancedMesh3D";
}

bool InstancedMesh3DGizmoPlugin::_has_gizmo(Node3D *p_node) const {
  return Object::cast_to<InstancedMesh3D>(p_node) != nullptr;
}

void InstancedMesh3DGizmoPlugin::_redraw(
    const Ref<EditorNode3DGizmo> &p_gizmo) {
  p_gizmo->clear();
  InstancedMesh3D *node =
      Object::cast_to<InstancedMesh3D>(p_gizmo->get_node_3d());
  Ref<Mesh> mesh = node ? node->get_mesh() : Ref<Mesh>();
  if (mesh.is_null()) {
    return;
  }
  p_gizmo->add_collision_triangles(mesh->generate_triangle_mesh());

  AABB box = mesh->get_aabb();
  PackedVector3Array lines;
  for (int i = 0; i < 12; i++) {
    Vector3 a, b;
    box.get_edge(i, a, b);
    lines.push_back(a);
    lines.push_back(b);
  }
  p_gizmo->add_lines(lines, get_material("bounds", p_gizmo));
}

void InstancedMeshEditorPlugin::on_settings_changed() {
  gizmo_plugin->load_color();
  Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
  if (!root) {
    return;
  }
  TypedArray<Node> nodes = root->find_children("*", "InstancedMesh3D", true, false);
  nodes.push_back(root);
  for (int i = 0; i < nodes.size(); i++) {
    if (InstancedMesh3D *n = Object::cast_to<InstancedMesh3D>(nodes[i])) {
      n->update_gizmos();
    }
  }
}

void InstancedMeshEditorPlugin::_notification(int p_what) {
  Callable on_change = callable_mp(this, &InstancedMeshEditorPlugin::on_settings_changed);
  if (p_what == NOTIFICATION_ENTER_TREE) {
    gizmo_plugin.instantiate();
    add_node_3d_gizmo_plugin(gizmo_plugin);
    ProjectSettings::get_singleton()->connect("settings_changed", on_change);
  } else if (p_what == NOTIFICATION_EXIT_TREE) {
    ProjectSettings::get_singleton()->disconnect("settings_changed", on_change);
    remove_node_3d_gizmo_plugin(gizmo_plugin);
    gizmo_plugin.unref();
  }
}

} // namespace godot

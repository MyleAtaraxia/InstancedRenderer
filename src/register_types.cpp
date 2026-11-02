#include "register_types.h"

#include "instance_definition_2d.h"
#include "instance_manager.h"
#include "instance_manager_2d.h"
#include "instanced_mesh_3d.h"
#include "instanced_mesh_3d_gizmo.h"
#include "instanced_node_2d.h"
#include "instanced_renderer_3d.h"

#include <gdextension_interface.h>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static InstanceManager *instance_manager = nullptr;
static InstanceManager2D *instance_manager_2d = nullptr;

void initialize_instanced_renderer_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(InstancedMesh3DGizmoPlugin);
		GDREGISTER_INTERNAL_CLASS(InstancedMeshEditorPlugin);
		EditorPlugins::add_by_type<InstancedMeshEditorPlugin>();
		return;
	}
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(InstancedRenderer3D);
	GDREGISTER_CLASS(InstancedMeshLOD);
	GDREGISTER_CLASS(InstancedMesh3D);
	GDREGISTER_CLASS(InstanceManager);
	GDREGISTER_CLASS(InstanceDefinition2D);
	GDREGISTER_ABSTRACT_CLASS(InstancedNode2D);
	GDREGISTER_CLASS(InstancedSprite2D);
	GDREGISTER_CLASS(InstancedMesh2D);
	GDREGISTER_CLASS(InstanceManager2D);

	InstanceManager::register_project_settings();
	InstanceManager2D::register_project_settings();

	instance_manager = memnew(InstanceManager);
	Engine::get_singleton()->register_singleton("InstanceManager", instance_manager);

	instance_manager_2d = memnew(InstanceManager2D);
	Engine::get_singleton()->register_singleton("InstanceManager2D", instance_manager_2d);
}

void uninitialize_instanced_renderer_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::remove_by_type<InstancedMeshEditorPlugin>();
		return;
	}
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	Engine::get_singleton()->unregister_singleton("InstanceManager2D");
	memdelete(instance_manager_2d);
	instance_manager_2d = nullptr;

	Engine::get_singleton()->unregister_singleton("InstanceManager");
	memdelete(instance_manager);
	instance_manager = nullptr;
}

extern "C" {
GDExtensionBool GDE_EXPORT library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_instanced_renderer_module);
	init_obj.register_terminator(uninitialize_instanced_renderer_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}

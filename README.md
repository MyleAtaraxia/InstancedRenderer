# InstancedRenderer3D

GPU-instanced mesh rendering for Godot 4 as a GDExtension. `InstancedMesh3D`
nodes are batched into MultiMeshes by world, mesh, material and spatial chunk,
with LOD, physics interpolation and sleeping of resting instances. Works in the
editor and at runtime.

Requires Godot 4.4+

## Install

Download the zip from the latest GitHub release and extract it into your
project, or copy `demo/addons/instanced_renderer`.

## Build

```bash
git submodule update --init
scons
```

## Usage

Add an `InstancedMesh3D` node and set its `mesh`. Nodes with matching settings
in the same chunk share one draw call.

- `is_static`: the node frees itself at runtime; its instance stays drawn. In 2D, exports replace it with packed transforms instead, unless `instanced_renderer/2d/bake_static_on_export` is off.
- `chunk_size`: batching grid size; also how fine culling and LOD are.
- `lods`: `InstancedMeshLOD` entries (`mesh`, `distance`); `mesh` is level 0.
- `visibility_range_end`: stop drawing beyond this distance.

Without nodes, through `InstanceManager3D` (or `InstanceManager2D` with
`InstanceDefinition2D`, `Transform2D` and `Vector2`; the methods match, except
that 2D `add_instance` takes a parent `CanvasItem` where 3D takes a `World3D`).
3D transforms are in world space, 2D ones relative to the parent. Instances
added this way must be removed.

```gdscript
var definition := InstanceDefinition3D.new()
definition.mesh = mesh
var id := InstanceManager3D.add_instance(get_world_3d(), definition, transform)
InstanceManager3D.update_instance(id, new_transform)
InstanceManager3D.remove_instance(id)
```

Bulk updates take a `PackedInt64Array` of ids:

- `update_instances(ids, transforms)`: one `Transform3D` per id.
- `update_instance_positions(ids, positions)`: `PackedVector3Array`; keeps rotation and scale. The fastest.
- `update_instance_rotations(ids, rotations)`: Euler radians (YXZ, like `Node3D.rotation`); keeps position and scale.
- `update_instances_packed(ids, floats)`: 12 floats per id in MultiMesh `TRANSFORM_3D` order.

Project settings are under `instanced_renderer/`; chunk sizes are
`instanced_renderer/2d/default_chunk_size` and `instanced_renderer/3d/default_chunk_size`.

![InstancedRenderer3D screenshot](assets/screenshot.webp)

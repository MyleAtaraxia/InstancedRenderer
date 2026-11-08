# godot --path demo --script res://scenes/instanced_3d_test.gd
extends SceneTree

const RED := Color(1, 0, 0)
const BLUE := Color(0, 0, 1)
const CLEAR := Color(0, 0, 0)

var failures := 0
var camera: Camera3D


func check(ok: bool, what: String) -> void:
	if not ok:
		failures += 1
	print("%s %s" % ["ok  " if ok else "FAIL", what])


func unshaded(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = color
	return material


# A 1 x 0.2 x 0.2 bar along x, so rotation and scale show on screen.
func bar_definition(color: Color) -> InstanceDefinition3D:
	var box := BoxMesh.new()
	box.size = Vector3(1, 0.2, 0.2)
	var d := InstanceDefinition3D.new()
	d.mesh = box
	d.material = unshaded(color)
	return d


# Interpolated batches only update on physics ticks.
func frames(count := 4) -> void:
	for i in count:
		await physics_frame
		await RenderingServer.frame_post_draw


func pixel_at(world_position: Vector3) -> Color:
	var screen := camera.unproject_position(world_position)
	return root.get_texture().get_image().get_pixelv(Vector2i(screen))


func is_color(actual: Color, expected: Color) -> bool:
	return actual.is_equal_approx(expected)


func drawn(world_position: Vector3, color := RED) -> bool:
	return is_color(pixel_at(world_position), color)


func world() -> World3D:
	return root.get_world_3d()


func _initialize() -> void:
	RenderingServer.set_default_clear_color(CLEAR)
	camera = Camera3D.new()
	camera.position = Vector3(0, 0, 10)
	root.add_child(camera)
	run.call_deferred()


func run() -> void:
	await frames()
	await test_add_update_remove()
	await test_chunk_crossing()
	await test_positions_rotations_packed()
	await test_swap_remove()
	await test_definition_edit()
	await test_sleep_and_static()
	await test_nodes()
	test_api_shape()
	print("failures: %d" % failures)
	quit(1 if failures > 0 else 0)


func test_add_update_remove() -> void:
	var d := bar_definition(RED)
	var id := InstanceManager3D.add_instance(world(), d, Transform3D())
	await frames()
	check(drawn(Vector3.ZERO), "add_instance draws")
	check(InstanceManager3D.get_batch_count() == 1, "one batch")

	InstanceManager3D.update_instance(id, Transform3D(Basis(), Vector3(0, 2, 0)))
	await frames()
	check(drawn(Vector3(0, 2, 0)), "update_instance moves it")
	check(drawn(Vector3.ZERO, CLEAR), "nothing left at the old spot")

	var ids := PackedInt64Array([id])
	var transforms: Array[Transform3D] = [Transform3D(Basis(), Vector3(0, -2, 0))]
	InstanceManager3D.update_instances(ids, transforms)
	await frames()
	check(drawn(Vector3(0, -2, 0)), "update_instances applies the transform")

	InstanceManager3D.remove_instance(id)
	await frames()
	check(not InstanceManager3D.is_instance_valid(id), "removed id goes stale")
	check(drawn(Vector3(0, -2, 0), CLEAR), "removed instance stops drawing")
	check(InstanceManager3D.get_batch_count() == 0, "empty batch is dropped")


func test_chunk_crossing() -> void:
	var d := bar_definition(RED)
	var id := InstanceManager3D.add_instance(world(), d, Transform3D())
	await frames()
	InstanceManager3D.update_instance(id, Transform3D(Basis(), Vector3(5000, 0, 0)))
	await frames()
	check(InstanceManager3D.is_instance_valid(id), "id survives a chunk crossing")
	check(drawn(Vector3.ZERO, CLEAR), "crossing leaves nothing behind")
	check(InstanceManager3D.get_batch_count() == 1, "the old chunk's batch is dropped")
	var positions := PackedVector3Array([Vector3(1, 0, 0)])
	InstanceManager3D.update_instance_positions(PackedInt64Array([id]), positions)
	await frames()
	check(drawn(Vector3(1, 0, 0)), "crossing back with positions redraws")
	# Straying just past a chunk edge keeps the batch.
	var neighbour := InstanceManager3D.add_instance(world(), d, Transform3D(Basis(), Vector3(0, 0.5, 0)))
	InstanceManager3D.update_instance(neighbour, Transform3D(Basis(), Vector3(0, -0.5, 0)))
	await frames()
	check(InstanceManager3D.get_batch_count() == 1, "hovering on a chunk edge keeps one batch")
	check(drawn(Vector3(0, -0.5, 0)), "and still draws where it went")
	InstanceManager3D.remove_instance(neighbour)
	InstanceManager3D.remove_instance(id)
	await frames()


func test_positions_rotations_packed() -> void:
	var d := bar_definition(RED)
	var stretched := Transform3D(Basis.from_scale(Vector3(3, 1, 1)), Vector3.ZERO)
	var id := InstanceManager3D.add_instance(world(), d, stretched)
	var ids := PackedInt64Array([id])
	await frames()
	check(drawn(Vector3(1.2, 0, 0)), "scaled bar reaches x = 1.2")
	check(drawn(Vector3(0, 1.2, 0), CLEAR), "and isn't vertical")

	InstanceManager3D.update_instance_rotations(ids, PackedVector3Array([Vector3(0, 0, PI / 2)]))
	await frames()
	check(drawn(Vector3(0, 1.2, 0)), "rotation turns it upright, scale kept")
	check(drawn(Vector3(1.2, 0, 0), CLEAR), "nothing left along x")

	InstanceManager3D.update_instance_positions(ids, PackedVector3Array([Vector3(2, 0, 0)]))
	await frames()
	check(drawn(Vector3(2, 1.2, 0)), "positions keep rotation and scale")
	check(drawn(Vector3(0, 1.2, 0), CLEAR), "positions move the origin")

	# Basis rows then origin: identity basis, origin (-2, 0, 0).
	var packed := PackedFloat32Array([1, 0, 0, -2, 0, 1, 0, 0, 0, 0, 1, 0])
	InstanceManager3D.update_instances_packed(ids, packed)
	await frames()
	check(drawn(Vector3(-2, 0, 0)), "packed sets the whole transform")
	check(drawn(Vector3(-2, 0.5, 0), CLEAR), "packed replaces rotation and scale")

	InstanceManager3D.update_instances_packed(ids, PackedFloat32Array([1, 2, 3]))
	check(InstanceManager3D.is_instance_valid(id), "a wrong-sized packed array is ignored")
	InstanceManager3D.remove_instance(id)
	await frames()


func test_swap_remove() -> void:
	var d := bar_definition(RED)
	var a := InstanceManager3D.add_instance(world(), d, Transform3D(Basis(), Vector3(-2, 0, 0)))
	var b := InstanceManager3D.add_instance(world(), d, Transform3D(Basis(), Vector3(2, 0, 0)))
	await frames()
	InstanceManager3D.remove_instance(a)
	await frames()
	check(InstanceManager3D.is_instance_valid(b), "swap-remove keeps the other id")
	check(drawn(Vector3(2, 0, 0)), "the other instance still draws")
	InstanceManager3D.update_instance(b, Transform3D(Basis(), Vector3(0, 2, 0)))
	await frames()
	check(drawn(Vector3(0, 2, 0)), "swapped instance still updates")
	InstanceManager3D.remove_instance(b)
	await frames()


func test_definition_edit() -> void:
	var d := bar_definition(RED)
	var id := InstanceManager3D.add_instance(world(), d, Transform3D())
	await frames()
	d.material = unshaded(BLUE)
	await frames()
	check(drawn(Vector3.ZERO, BLUE), "definition edits apply to live instances")
	check(InstanceManager3D.is_instance_valid(id), "ids survive a definition edit")
	(d.mesh as BoxMesh).size = Vector3(3, 0.2, 0.2)
	await frames()
	check(drawn(Vector3(1.2, 0, 0), BLUE), "editing the mesh in place applies too")
	InstanceManager3D.remove_instance(id)
	await frames()


func test_sleep_and_static() -> void:
	var d := bar_definition(RED)
	# Both in the chunk at the origin, so they can share a batch.
	var resting := InstanceManager3D.add_instance(world(), d, Transform3D(Basis(), Vector3(0.5, 0, 0)))
	var frozen := InstanceManager3D.add_instance(world(), d, Transform3D(Basis(), Vector3(2, 0, 0)), true)
	var after: int = ProjectSettings.get_setting("instanced_renderer/batching/sleep_after_frames")
	await frames(after + 10)
	check(InstanceManager3D.is_instance_valid(resting), "sleeping id stays valid")
	check(drawn(Vector3(0.5, 0, 0)), "sleeping instance stays drawn")
	check(drawn(Vector3(2, 0, 0)), "static instance draws")
	check(InstanceManager3D.get_batch_count() == 1, "sleeping and static share the frozen batch")
	InstanceManager3D.update_instance(resting, Transform3D(Basis(), Vector3(0.5, 2, 0)))
	await frames()
	check(drawn(Vector3(0.5, 2, 0)), "waking moves it")
	check(drawn(Vector3(0.5, 0, 0), CLEAR), "waking leaves nothing behind")
	check(InstanceManager3D.get_batch_count() == 2, "the woken instance leaves the frozen batch")
	InstanceManager3D.update_instance(frozen, Transform3D(Basis(), Vector3(2, 2, 0)))
	await frames()
	check(drawn(Vector3(2, 2, 0)), "static instances can still be moved")
	InstanceManager3D.remove_instance(resting)
	InstanceManager3D.remove_instance(frozen)
	await frames()


func test_nodes() -> void:
	var parent := Node3D.new()
	root.add_child(parent)
	var box := BoxMesh.new()
	box.size = Vector3(1, 0.2, 0.2)
	var red := unshaded(RED)
	var node := InstancedMesh3D.new()
	node.mesh = box
	node.material = red
	parent.add_child(node)
	var frozen := InstancedMesh3D.new()
	frozen.mesh = box
	frozen.material = red
	frozen.is_static = true
	frozen.position = Vector3(2, 0, 0)
	parent.add_child(frozen)
	await frames()
	check(drawn(Vector3.ZERO), "InstancedMesh3D draws")
	check(not is_instance_valid(frozen), "static node frees itself")
	check(drawn(Vector3(2, 0, 0)), "static instance outlives its node")

	var batches := InstanceManager3D.get_batch_count()
	var twin := InstancedMesh3D.new()
	twin.mesh = box
	twin.material = red
	twin.position = Vector3(0.5, 1, 0) # same chunk as `node`
	parent.add_child(twin)
	await frames()
	check(InstanceManager3D.get_batch_count() == batches, "equal nodes share a batch")
	twin.cast_shadow = 0
	await frames()
	check(InstanceManager3D.get_batch_count() == batches + 1, "a differing node gets its own batch")
	twin.free()
	await frames()

	node.position = Vector3(0, 2, 0)
	await frames()
	check(drawn(Vector3(0, 2, 0)), "node movement follows")
	check(drawn(Vector3.ZERO, CLEAR), "a node owns one instance, nothing left behind")
	parent.position = Vector3(-2, 0, 0)
	await frames()
	check(drawn(Vector3(-2, 2, 0)), "parent transform is inherited")
	node.hide()
	await frames()
	check(drawn(Vector3(-2, 2, 0), CLEAR), "hidden node stops drawing")
	node.show()
	await frames()
	check(drawn(Vector3(-2, 2, 0)), "shown node draws again")
	node.free()
	await frames()
	check(drawn(Vector3(-2, 2, 0), CLEAR), "freed node removes its instance")
	parent.free()
	await frames()


func test_api_shape() -> void:
	check(Engine.has_singleton("InstanceManager3D"), "InstanceManager3D is a singleton")
	check(not Engine.has_singleton("InstanceManager"), "InstanceManager is gone")
	check(not ClassDB.class_exists("InstancedRenderer3D"), "InstancedRenderer3D is internal")
	for method in ["add_instance", "update_instance", "update_instances",
			"update_instance_positions", "update_instance_rotations",
			"update_instances_packed", "remove_instance",
			"reset_instance_interpolation", "is_instance_valid", "get_batch_count"]:
		check(ClassDB.class_has_method("InstanceManager2D", method)
				and ClassDB.class_has_method("InstanceManager3D", method),
				"both managers have %s" % method)
	check(ProjectSettings.has_setting("instanced_renderer/2d/default_chunk_size"), "2D chunk size setting")
	check(ProjectSettings.has_setting("instanced_renderer/3d/default_chunk_size"), "3D chunk size setting")

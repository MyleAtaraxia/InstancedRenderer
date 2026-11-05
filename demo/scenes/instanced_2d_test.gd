# godot --path demo --script res://scenes/instanced_2d_test.gd
extends SceneTree

const RED := Color(1, 0, 0)
const CANVAS_ECHO := "instanced_renderer/2d/repeat_uploads_for_motion_vectors"
const BLUE := Color(0, 0, 1)
const CLEAR := Color(0, 0, 0)

var failures := 0


func check(ok: bool, what: String) -> void:
	if not ok:
		failures += 1
	print("%s %s" % ["ok  " if ok else "FAIL", what])


func solid(size: Vector2i, color: Color) -> Texture2D:
	var image := Image.create(size.x, size.y, false, Image.FORMAT_RGBA8)
	image.fill(color)
	return ImageTexture.create_from_image(image)


func definition(texture: Texture2D, y_sorted := false, offset := Vector2.ZERO) -> InstanceDefinition2D:
	var d := InstanceDefinition2D.new()
	d.texture = texture
	d.y_sorted = y_sorted
	d.offset = offset
	return d


func quad_definition(texture: Texture2D, y_sorted: bool) -> InstanceDefinition2D:
	var quad := QuadMesh.new()
	quad.size = Vector2(32, 32)
	var d := definition(texture, y_sorted)
	d.mesh = quad
	return d


# Interpolated batches and Camera2D only update on physics ticks.
func frames(count := 4) -> void:
	for i in count:
		await physics_frame
		await RenderingServer.frame_post_draw


func pixel(position: Vector2) -> Color:
	return root.get_texture().get_image().get_pixelv(Vector2i(position))


func is_color(actual: Color, expected: Color) -> bool:
	return actual.is_equal_approx(expected)


func new_parent(y_sort := false) -> Node2D:
	var parent := Node2D.new()
	parent.y_sort_enabled = y_sort
	root.add_child(parent)
	return parent


func _initialize() -> void:
	RenderingServer.set_default_clear_color(CLEAR)
	run.call_deferred()


func run() -> void:
	await frames()
	await test_quad_size_and_modulate(false)
	await test_quad_size_and_modulate(true)
	await test_meshes()
	await test_y_order()
	await test_culling()
	await test_tall_sprite()
	await test_ids()
	await test_toggle_and_parent_free()
	await test_sleep()
	await test_swap_remove_does_not_blink()
	await test_nodes()
	await test_node_children()
	await test_pixels_per_meter(false)
	await test_pixels_per_meter(true)
	await test_mesh_scale_partial_update()
	await test_last_partial_update_with_taa()
	await test_texture_settings(false)
	await test_texture_settings(true)
	test_inspector_layout()
	print("failures: %d" % failures)
	quit(1 if failures > 0 else 0)


func test_quad_size_and_modulate(y_sorted: bool) -> void:
	var parent := new_parent()
	var d := definition(solid(Vector2i(40, 20), Color.WHITE), y_sorted, Vector2(0, -10))
	d.modulate = RED
	InstanceManager2D.add_instance(parent, d, Transform2D(0, Vector2(100, 100)))
	await frames()
	var mode := "y_sorted" if y_sorted else "chunked"
	check(is_color(pixel(Vector2(100, 90)), RED), "%s: draws modulated at offset" % mode)
	check(is_color(pixel(Vector2(82, 82)), RED), "%s: quad reaches the texture's corner" % mode)
	check(is_color(pixel(Vector2(78, 90)), CLEAR), "%s: quad stops at texture width" % mode)
	check(is_color(pixel(Vector2(100, 102)), CLEAR), "%s: quad stops at texture height" % mode)
	parent.free()
	await frames()


func test_meshes() -> void:
	var parent := new_parent()
	InstanceManager2D.add_instance(parent, quad_definition(solid(Vector2i(4, 4), RED), false), Transform2D(0, Vector2(100, 100)))
	InstanceManager2D.add_instance(parent, quad_definition(solid(Vector2i(4, 4), BLUE), true), Transform2D(0, Vector2(200, 100)))
	await frames()
	check(is_color(pixel(Vector2(100, 100)), RED), "chunked mesh draws")
	check(is_color(pixel(Vector2(200, 100)), BLUE), "y_sorted mesh draws")
	parent.free()
	await frames()


# The later-added sprite is higher up, so only Y-sort puts the other on top.
func test_y_order() -> void:
	var parent := new_parent(true)
	var red := definition(solid(Vector2i(64, 64), RED), true)
	var blue := definition(solid(Vector2i(64, 64), BLUE), true)
	InstanceManager2D.add_instance(parent, red, Transform2D(0, Vector2(100, 120)))
	InstanceManager2D.add_instance(parent, blue, Transform2D(0, Vector2(100, 100)))
	var normal := Sprite2D.new()
	normal.texture = solid(Vector2i(64, 64), Color.GREEN)
	normal.position = Vector2(100, 110)
	parent.add_child(normal)
	await frames()
	check(is_color(pixel(Vector2(100, 145)), RED), "lowest instance draws on top")
	check(is_color(pixel(Vector2(100, 75)), BLUE), "highest instance stays underneath")
	check(is_color(pixel(Vector2(100, 88 + 50)), RED), "normal sprite sorts below the lower instance")
	check(is_color(pixel(Vector2(100, 85 + 50 - 60)), BLUE), "only the top sliver of the highest shows")
	normal.position = Vector2(100, 130)
	await frames()
	check(is_color(pixel(Vector2(100, 150)), Color.GREEN), "instances sort against normal nodes")
	parent.free()
	await frames()


func test_culling() -> void:
	var parent := new_parent(true)
	var d := definition(solid(Vector2i(16, 16), RED), true)
	var ids := []
	for x in 100:
		for y in 100:
			ids.append(InstanceManager2D.add_instance(parent, d, Transform2D(0, Vector2(x, y) * 200.0)))
	await frames()
	var seen := InstanceManager2D.get_rendered_item_count()
	check(seen > 0 and seen < 500, "only in-view chunks get items (%d of 10000)" % seen)

	var camera := Camera2D.new()
	root.add_child(camera)
	camera.make_current()
	camera.global_position = Vector2(100000, 100000)
	await frames()
	check(InstanceManager2D.get_rendered_item_count() == 0, "no items once the camera leaves")

	camera.global_position = Vector2(4000, 4000)
	await frames()
	check(InstanceManager2D.get_rendered_item_count() > 0, "items return with the camera")
	var center := Vector2(root.get_visible_rect().size) / 2
	check(is_color(pixel(center), RED), "instance under the camera is drawn")
	parent.position = Vector2(100000, 0)
	await frames()
	check(InstanceManager2D.get_rendered_item_count() == 0, "moving the parent re-culls")
	camera.free()
	parent.free()
	await frames()
	check(InstanceManager2D.get_rendered_item_count() == 0, "freeing the parent releases items")
	check(not InstanceManager2D.is_instance_valid(ids[0]), "freeing the parent drops ids")


# Its origin is far below the view, in a chunk with nothing else on screen.
func test_tall_sprite() -> void:
	var parent := new_parent()
	var d := definition(solid(Vector2i(16, 2000), RED), true, Vector2(0, -1000))
	InstanceManager2D.add_instance(parent, d, Transform2D(0, Vector2(100, 2200)))
	await frames()
	check(is_color(pixel(Vector2(100, 300)), RED), "off-screen origin still shows a tall sprite")
	parent.free()
	await frames()


func test_ids() -> void:
	var parent := new_parent()
	var d := definition(solid(Vector2i(16, 16), RED), true)
	var a := InstanceManager2D.add_instance(parent, d, Transform2D(0, Vector2(100, 100)))
	var b := InstanceManager2D.add_instance(parent, d, Transform2D(0, Vector2(140, 100)))
	await frames()
	InstanceManager2D.update_instance(a, Transform2D(0, Vector2(5000, 100)))
	await frames()
	check(InstanceManager2D.is_instance_valid(a), "id survives a chunk crossing")
	check(is_color(pixel(Vector2(100, 100)), CLEAR), "crossing leaves nothing behind")
	InstanceManager2D.update_instance(a, Transform2D(0, Vector2(100, 200)))
	await frames()
	check(is_color(pixel(Vector2(100, 200)), RED), "crossing back redraws")
	InstanceManager2D.remove_instance(a)
	check(not InstanceManager2D.is_instance_valid(a), "removed id goes stale")
	check(InstanceManager2D.is_instance_valid(b), "swap-remove keeps the other id")
	InstanceManager2D.update_instance(b, Transform2D(0, Vector2(300, 300)))
	await frames()
	check(is_color(pixel(Vector2(300, 300)), RED), "swapped instance still updates")

	var ids := PackedInt64Array([b])
	var floats := PackedFloat32Array([1, 0, 0, 400, 0, 1, 0, 300])
	InstanceManager2D.update_instances(ids, floats)
	await frames()
	check(is_color(pixel(Vector2(400, 300)), RED), "update_instances uses the multimesh layout")
	parent.free()
	await frames()


func test_toggle_and_parent_free() -> void:
	var parent := new_parent()
	var d := definition(solid(Vector2i(16, 16), RED), true)
	var id := InstanceManager2D.add_instance(parent, d, Transform2D(0, Vector2(100, 100)))
	await frames()
	check(InstanceManager2D.get_rendered_item_count() == 1, "y_sorted uses one item")
	d.y_sorted = false
	await frames()
	check(InstanceManager2D.is_instance_valid(id), "id survives the toggle")
	check(InstanceManager2D.get_rendered_item_count() == 0, "chunked uses no pooled items")
	check(is_color(pixel(Vector2(100, 100)), RED), "still drawn after the toggle")
	d.texture = solid(Vector2i(16, 16), BLUE)
	await frames()
	check(is_color(pixel(Vector2(100, 100)), BLUE), "definition edits apply to live instances")
	parent.free()
	await frames()
	check(not InstanceManager2D.is_instance_valid(id), "chunked ids drop with the parent")


func test_sleep() -> void:
	var parent := new_parent()
	var d := definition(solid(Vector2i(16, 16), RED))
	var awake := InstanceManager2D.add_instance(parent, d, Transform2D(0, Vector2(100, 100)))
	var other := InstanceManager2D.add_instance(parent, d, Transform2D(0, Vector2(140, 100)))
	await frames(70)
	check(is_color(pixel(Vector2(100, 100)), RED), "sleeping instance stays drawn")
	InstanceManager2D.update_instance(awake, Transform2D(0, Vector2(100, 200)))
	await frames()
	check(is_color(pixel(Vector2(100, 200)), RED), "waking moves it")
	check(is_color(pixel(Vector2(100, 100)), CLEAR), "waking leaves nothing behind")
	check(is_color(pixel(Vector2(140, 100)), RED), "neighbour keeps sleeping")
	check(InstanceManager2D.is_instance_valid(other), "sleeping id stays valid")
	parent.free()
	await frames()


# Removing or sleeping a neighbour moves the survivor to another buffer index.
func test_swap_remove_does_not_blink() -> void:
	var d := definition(solid(Vector2i(16, 16), RED))
	var blinks := 0
	for trial in 30:
		var parent := new_parent()
		var gone := InstanceManager2D.add_instance(parent, d, Transform2D(0, Vector2(100, 100)))
		InstanceManager2D.add_instance(parent, d, Transform2D(0, Vector2(100, 200)))
		await frames()
		if trial % 2 == 0:
			InstanceManager2D.remove_instance(gone)
		for i in 80 if trial % 2 == 1 else 6:
			await frames(1)
			if not is_color(pixel(Vector2(100, 200)), RED):
				blinks += 1
		parent.free()
		await frames(2)
	check(blinks == 0, "survivor never blinks (%d missed frames)" % blinks)


func test_pixels_per_meter(y_sorted: bool) -> void:
	var parent := new_parent()
	var mesh := InstancedMesh2D.new()
	mesh.mesh = quad_definition(null, y_sorted).mesh # 32 units wide
	mesh.instance_modulate = BLUE
	mesh.y_sorted = y_sorted
	mesh.pixels_per_meter = 2.0
	mesh.position = Vector2(200, 200)
	parent.add_child(mesh)
	await frames()
	check(is_color(pixel(Vector2(228, 200)), BLUE), "pixels_per_meter scales the mesh")
	check(is_color(pixel(Vector2(236, 200)), CLEAR), "pixels_per_meter scales it no further")
	mesh.position = Vector2(300, 200)
	mesh.scale = Vector2(0.5, 0.5)
	await frames()
	check(is_color(pixel(Vector2(312, 200)), BLUE), "it keeps applying after moving, on top of node scale")
	check(is_color(pixel(Vector2(320, 200)), CLEAR), "node scale isn't applied twice")
	await frames(30) # sleeping re-places the instance from its stored transform
	check(is_color(pixel(Vector2(312, 200)), BLUE), "it survives falling asleep")
	check(is_color(pixel(Vector2(320, 200)), CLEAR), "falling asleep doesn't scale it twice")
	parent.free()
	await frames()


func test_texture_settings(y_sorted: bool) -> void:
	var image := Image.create(2, 1, false, Image.FORMAT_RGBA8)
	image.set_pixel(0, 0, RED)
	image.set_pixel(1, 0, BLUE)
	var parent := new_parent()
	var sprite := InstancedSprite2D.new()
	sprite.texture = ImageTexture.create_from_image(image)
	sprite.y_sorted = y_sorted
	sprite.position = Vector2(200, 200)
	sprite.scale = Vector2(16, 16) # 32 wide, the texels meet at x = 200
	sprite.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	parent.add_child(sprite)
	await frames()
	check(pixel(Vector2(199, 200)).b < 0.05, "nearest keeps texels sharp")
	sprite.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
	await frames()
	check(pixel(Vector2(199, 200)).b > 0.2, "changing to linear blends them")
	sprite.texture_filter = CanvasItem.TEXTURE_FILTER_PARENT_NODE
	parent.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	await frames()
	check(pixel(Vector2(199, 200)).b < 0.05, "filter inherits from the parent")
	parent.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
	await frames()
	check(pixel(Vector2(199, 200)).b > 0.2, "a parent change reaches the instance")

	var shader := Shader.new()
	shader.code = "shader_type canvas_item; void fragment() { COLOR = texture(TEXTURE, UV + vec2(0.75, 0.0)); }"
	var shifted := ShaderMaterial.new()
	shifted.shader = shader
	parent.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	sprite.instance_material = shifted
	sprite.texture_repeat = CanvasItem.TEXTURE_REPEAT_DISABLED
	await frames()
	var sample := Vector2(204, 200) # UV 0.625, read at 1.375
	check(is_color(pixel(sample), BLUE), "disabled repeat clamps")
	sprite.texture_repeat = CanvasItem.TEXTURE_REPEAT_ENABLED
	await frames()
	check(is_color(pixel(sample), RED), "enabled repeat wraps")
	sprite.texture_repeat = CanvasItem.TEXTURE_REPEAT_MIRROR
	await frames()
	check(is_color(pixel(sample), BLUE), "mirror repeat reflects")
	sprite.texture_repeat = CanvasItem.TEXTURE_REPEAT_PARENT_NODE
	parent.texture_repeat = CanvasItem.TEXTURE_REPEAT_ENABLED
	await frames()
	check(is_color(pixel(sample), RED), "repeat inherits from the parent")
	parent.free()
	await frames()


# With interpolation off, as in the editor, a few moved instances update one at
# a time instead of through a full upload.
func test_mesh_scale_partial_update() -> void:
	physics_interpolation = false
	root.use_taa = false # motion vectors hide every other partial update from the canvas
	var parent := new_parent()
	var mesh := InstancedMesh2D.new()
	mesh.mesh = quad_definition(null, false).mesh # 32 units wide
	mesh.instance_modulate = BLUE
	mesh.pixels_per_meter = 2.0
	mesh.position = Vector2(140, 200)
	parent.add_child(mesh)
	await frames()
	mesh.position = Vector2(150, 200) # waking is a full upload, so is changing chunk
	await frames(1)
	mesh.position = Vector2(160, 200)
	await frames(1)
	check(is_color(pixel(Vector2(188, 200)), BLUE), "a partial update keeps pixels_per_meter")
	check(is_color(pixel(Vector2(196, 200)), CLEAR), "a partial update doesn't scale it further")
	parent.free()
	await frames()
	root.use_taa = true
	physics_interpolation = true


# TAA's motion vectors hide every other multimesh write from the canvas, so a
# move that stops must not end on the hidden one.
func test_last_partial_update_with_taa() -> void:
	if not ProjectSettings.has_setting(CANVAS_ECHO):
		print("skip canvas echo not built in")
		return
	check(ProjectSettings.get_setting(CANVAS_ECHO), "the canvas echo defaults to on")
	check(await stopped_moves_lost() == 0, "with the echo, every stopped move shows up")
	ProjectSettings.set_setting(CANVAS_ECHO, false)
	await frames()
	check(await stopped_moves_lost() > 0, "the setting turns the echo off")
	ProjectSettings.set_setting(CANVAS_ECHO, true)
	await frames()
	check(await stopped_moves_lost() == 0, "the setting turns the echo back on")


func stopped_moves_lost() -> int:
	physics_interpolation = false
	var parent := new_parent()
	var sprite := InstancedSprite2D.new()
	sprite.texture = solid(Vector2i(16, 16), RED)
	sprite.position = Vector2(140, 200)
	parent.add_child(sprite)
	await frames()
	sprite.position = Vector2(150, 200) # waking is a full upload, so is changing chunk
	await frames(1)
	var target := sprite.position
	var lost := 0
	for i in 4:
		target.x += 10
		sprite.position = target
		await frames(2)
		if not is_color(pixel(target), RED):
			lost += 1
	parent.free()
	await frames()
	physics_interpolation = true
	return lost


func test_inspector_layout() -> void:
	var node := InstancedSprite2D.new()
	var shown := {}
	for property in node.get_property_list():
		if property.usage & PROPERTY_USAGE_EDITOR:
			shown[property.name] = true
	check(shown.has("instance_material"), "the working material is listed")
	for hidden in ["material", "use_parent_material", "modulate", "self_modulate", "z_index", "z_as_relative"]:
		check(not shown.has(hidden), "inherited %s is hidden" % hidden)
	node.free()


# Childless nodes leave the canvas tree, which mustn't lose anything under them.
func test_node_children() -> void:
	var parent := new_parent()
	var sprite := InstancedSprite2D.new()
	sprite.texture = solid(Vector2i(16, 16), RED)
	sprite.position = Vector2(100, 100)
	parent.add_child(sprite)
	await frames()
	var child := Sprite2D.new()
	child.texture = solid(Vector2i(16, 16), BLUE)
	child.position = Vector2(0, 50)
	sprite.add_child(child)
	await frames()
	check(is_color(pixel(Vector2(100, 150)), BLUE), "a child added later draws")
	sprite.position = Vector2(200, 100)
	await frames()
	check(is_color(pixel(Vector2(200, 150)), BLUE), "the child follows its node")
	check(is_color(pixel(Vector2(200, 100)), RED), "the instance follows too")
	sprite.remove_child(child)
	await frames()
	check(is_color(pixel(Vector2(200, 100)), RED), "losing the child keeps the instance")
	sprite.add_child(child)
	await frames()
	check(is_color(pixel(Vector2(200, 150)), BLUE), "a child can come back")

	var other := new_parent()
	sprite.reparent(other, false)
	sprite.position = Vector2(300, 100)
	await frames()
	check(is_color(pixel(Vector2(300, 150)), BLUE), "a child survives a reparent")
	sprite.hide()
	await frames()
	check(is_color(pixel(Vector2(300, 150)), CLEAR), "hiding the node hides the child")
	sprite.show()
	child.free()
	await frames()
	check(is_color(pixel(Vector2(300, 100)), RED), "a freed child keeps the instance")

	var loose := Sprite2D.new()
	loose.texture = solid(Vector2i(16, 16), BLUE)
	var top := InstancedSprite2D.new()
	top.texture = sprite.texture
	top.position = Vector2(400, 100)
	other.add_child(top)
	await frames()
	top.top_level = true
	top.add_child(loose)
	await frames()
	check(is_color(pixel(Vector2(400, 100)), BLUE), "a top level node keeps its child")
	top.top_level = false
	await frames()
	check(is_color(pixel(Vector2(400, 100)), BLUE), "leaving top level keeps the child")
	parent.free()
	other.free()
	await frames()


func test_nodes() -> void:
	var parent := new_parent()
	var sprite := InstancedSprite2D.new()
	sprite.texture = solid(Vector2i(16, 16), RED)
	sprite.position = Vector2(100, 100)
	parent.add_child(sprite)
	var mesh := InstancedMesh2D.new()
	mesh.mesh = quad_definition(null, true).mesh
	mesh.instance_modulate = BLUE
	mesh.y_sorted = true
	mesh.position = Vector2(200, 100)
	parent.add_child(mesh)
	var frozen := InstancedSprite2D.new()
	frozen.texture = sprite.texture
	frozen.is_static = true
	frozen.position = Vector2(300, 100)
	parent.add_child(frozen)
	await frames()
	check(is_color(pixel(Vector2(100, 100)), RED), "InstancedSprite2D draws")
	check(is_color(pixel(Vector2(200, 100)), BLUE), "InstancedMesh2D draws")
	check(not is_instance_valid(frozen), "static node frees itself")
	check(is_color(pixel(Vector2(300, 100)), RED), "static instance outlives its node")

	var batches: int = InstanceManager2D.get_batch_count()
	var twin := InstancedSprite2D.new()
	twin.texture = sprite.texture
	twin.position = Vector2(110, 100)
	parent.add_child(twin)
	await frames()
	check(InstanceManager2D.get_batch_count() == batches, "equal nodes share a batch")
	twin.instance_modulate = Color(1, 1, 1, 0.5)
	await frames()
	check(InstanceManager2D.get_batch_count() == batches + 1, "a differing node gets its own batch")
	twin.free()
	await frames()

	sprite.position = Vector2(100, 200)
	await frames()
	check(is_color(pixel(Vector2(100, 200)), RED), "node movement follows")
	check(is_color(pixel(Vector2(100, 100)), CLEAR), "a node owns one instance, nothing left behind")
	sprite.hide()
	await frames()
	check(is_color(pixel(Vector2(100, 200)), CLEAR), "hidden node stops drawing")
	sprite.show()
	await frames()
	check(is_color(pixel(Vector2(100, 200)), RED), "shown node draws again")
	parent.position = Vector2(50, 0)
	await frames()
	check(is_color(pixel(Vector2(150, 200)), RED), "parent transform is inherited")
	sprite.free()
	await frames()
	check(is_color(pixel(Vector2(150, 200)), CLEAR), "freed node removes its instance")
	parent.free()
	await frames()

extends Node3D

@export var grid_size := Vector2i(140, 140)
@export var spacing := 1.5
@export var amplitude := 2.6:
	set(value):
		amplitude = value
		wave.configure({amplitude = value})
@export var frequency := 0.11
@export var speed := 2.0
@export var updates_enabled := true
@export_range(0, 240) var updates_per_second := 30:
	set(value):
		updates_per_second = value
		if interpolate and value > 0 and is_node_ready():
			Engine.physics_ticks_per_second = value

@export var interpolate := true

var wave := SineGrid.new()
var ids := PackedInt64Array()
var time := 0.0
var since_update := 0.0
var wave_ms := 0.0
var gpu_ms := 0.0

@onready var pivot: Node3D = $CameraPivot
@onready var stats: Label = $UI/Stats

func _ready() -> void:
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	if interpolate:
		get_tree().physics_interpolation = true  # before add_instance(), which reads it
		if updates_per_second > 0:
			Engine.physics_ticks_per_second = updates_per_second
	wave.configure({
		size_x = grid_size.x, size_z = grid_size.y, spacing = spacing,
		amplitude = amplitude, frequency = frequency, speed = speed,
	})

	var box := BoxMesh.new()
	var material := StandardMaterial3D.new()
	material.albedo_color = Color("#fc7f7f")
	material.roughness = 0.55

	var definition := InstanceDefinition3D.new()
	definition.mesh = box
	definition.material = material

	var start: Array[Transform3D] = wave.step(time)
	for t in start:
		ids.append(InstanceManager3D.add_instance(get_world_3d(), definition, t))


func _exit_tree() -> void:
	for id in ids:
		InstanceManager3D.remove_instance(id)

func _physics_process(delta: float) -> void:
	pivot.rotate_y(delta * 0.1)
	_update_stats()
	if not updates_enabled:
		return
	time += delta
	since_update += delta
	var interval := 1.0 / updates_per_second if updates_per_second > 0 else 0.0
	if since_update >= interval:
		since_update = fmod(since_update, interval) if interval > 0.0 else 0.0
		var t0 := Time.get_ticks_usec()
		InstanceManager3D.update_instances(ids, wave.step(time))
		wave_ms = lerpf(wave_ms, (Time.get_ticks_usec() - t0) / 1000.0, 0.1)


func _update_stats() -> void:
	var gpu := RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
	gpu_ms = lerpf(gpu_ms, gpu, 0.1)
	var rate := "off"
	if updates_enabled:
		rate = "%d Hz" % updates_per_second if updates_per_second > 0 else "every tick"
		if interpolate:
			rate += ", interpolated"
	stats.text = "%d boxes  |  %d fps  |  gpu %.2f ms  |  wave %.2f ms" % [
		ids.size(), Engine.get_frames_per_second(), gpu_ms, wave_ms]

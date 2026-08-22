import unreal


TOON_FIRST_MAP = "/Game/Scenes/ToonFirst"

STABILITY_COMMANDS = [
    "r.VSync 0",
    "t.MaxFPS 0",
    "r.ScreenPercentage 100",
    "r.DynamicRes.OperationMode 0",
    "stat unit",
    "stat gpu",
    "stat scenerendering",
]

VIEW_MODE_COMMANDS = {
    "lit": "viewmode lit",
    "shader_complexity": "viewmode shadercomplexity",
    "quad_overdraw": "viewmode quadoverdraw",
}

CASES = {
    "baseline_high_day": {
        "preset": "apply_preset_day",
        "quality": "apply_high_pc",
        "state": None,
        "note": "HighPC + Day + Normal baseline.",
    },
    "baseline_mid_day": {
        "preset": "apply_preset_day",
        "quality": "apply_mid_pc",
        "state": None,
        "note": "MidPC downgrade comparison.",
    },
    "baseline_low_day": {
        "preset": "apply_preset_day",
        "quality": "apply_low_spec",
        "state": None,
        "note": "LowSpec readability floor.",
    },
    "high_indoor": {
        "preset": "apply_preset_indoor",
        "quality": "apply_high_pc",
        "state": None,
        "note": "Low-light environment check.",
    },
    "high_strong_color": {
        "preset": "apply_preset_strong_color_light",
        "quality": "apply_high_pc",
        "state": None,
        "note": "Strong colored light contamination check.",
    },
    "high_no_ambient": {
        "preset": "apply_preset_no_ambient",
        "quality": "apply_high_pc",
        "state": None,
        "note": "No ambient readability check.",
    },
    "high_hit_flash": {
        "preset": "apply_preset_day",
        "quality": "apply_high_pc",
        "state": "apply_state_hit_flash",
        "note": "Transient HitFlash state.",
    },
    "high_rim_boost": {
        "preset": "apply_preset_day",
        "quality": "apply_high_pc",
        "state": "apply_state_rim_boost",
        "note": "Persistent RimBoost state.",
    },
    "high_outline_boost": {
        "preset": "apply_preset_day",
        "quality": "apply_high_pc",
        "state": "apply_state_outline_boost",
        "note": "Persistent OutlineBoost state.",
    },
    "high_battle_tint": {
        "preset": "apply_preset_day",
        "quality": "apply_high_pc",
        "state": "apply_state_battle_tint",
        "note": "Persistent BattleTint state.",
    },
    "high_per_shadow": {
        "preset": "apply_preset_day",
        "quality": "apply_high_pc",
        "state": None,
        "note": "Manual PerShadow check. Confirm MF_PerShadow / RT_Daniya_Pershadow wiring before sampling.",
    },
}


def _get_all_level_actors():
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return actor_subsystem.get_all_level_actors()


def _find_controller():
    for actor in _get_all_level_actors():
        class_name = actor.get_class().get_name()
        if "RenderDemoController" in class_name:
            return actor

    for actor in _get_all_level_actors():
        if hasattr(actor, "apply_high_pc") and hasattr(actor, "apply_preset_day"):
            return actor

    return None


def _exec(command):
    controller = _find_controller()
    context = controller if controller else unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(context, command)
    unreal.log(f"[KuroPerf] Console: {command}")


def _call(controller, method_name):
    if not method_name:
        return

    method = getattr(controller, method_name, None)
    if not method:
        unreal.log_warning(f"[KuroPerf] Controller has no method: {method_name}")
        return

    method()
    unreal.log(f"[KuroPerf] Called: {method_name}")


def open_toon_first():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if level_subsystem:
        level_subsystem.load_level(TOON_FIRST_MAP)
        unreal.log(f"[KuroPerf] Opened map: {TOON_FIRST_MAP}")
        return

    unreal.log_warning("[KuroPerf] LevelEditorSubsystem is unavailable; open ToonFirst manually.")


def list_cases():
    unreal.log("[KuroPerf] Available cases:")
    for case_id, data in CASES.items():
        unreal.log(f"  {case_id}: {data['note']}")


def prepare_capture():
    # 只做稳定采样相关的 console command，不创建或保存任何资产。
    for command in STABILITY_COMMANDS:
        _exec(command)


def apply_case(case_id, prepare=True):
    if case_id not in CASES:
        unreal.log_warning(f"[KuroPerf] Unknown case: {case_id}")
        list_cases()
        return

    controller = _find_controller()
    if not controller:
        unreal.log_error("[KuroPerf] No RenderDemoController found in the current level.")
        return

    data = CASES[case_id]
    unreal.log(f"[KuroPerf] Applying case: {case_id} - {data['note']}")

    reset_method = getattr(controller, "reset_render_state", None)
    if reset_method:
        reset_method()
        unreal.log("[KuroPerf] Called: reset_render_state")

    _call(controller, data["preset"])
    _call(controller, data["quality"])
    _call(controller, data["state"])

    if prepare:
        prepare_capture()

    set_view_mode("lit")
    unreal.log("[KuroPerf] Case applied. Warm up for 5 seconds, then capture Lit / Shader Complexity / Quad Overdraw / GPU Visualizer.")


def set_view_mode(mode):
    command = VIEW_MODE_COMMANDS.get(mode)
    if not command:
        unreal.log_warning(f"[KuroPerf] Unknown view mode: {mode}. Use one of: {list(VIEW_MODE_COMMANDS.keys())}")
        return

    _exec(command)


def open_gpu_visualizer():
    _exec("profilegpu")


if __name__ == "__main__":
    list_cases()

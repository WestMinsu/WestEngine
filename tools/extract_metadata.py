#!/usr/bin/env python3
# =============================================================================
# WestEngine - Shader Tools
# Extract Slang reflection JSON into generated C++ metadata constants
# =============================================================================

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


def _sanitize_identifier(name: str) -> str:
    parts = re.split(r"[^A-Za-z0-9_]+", name)
    identifier = "".join(part[:1].upper() + part[1:] for part in parts if part)
    if not identifier:
        return "UnknownShader"
    if identifier[0].isdigit():
        return f"Shader{identifier}"
    return identifier


def _shader_name_from_reflection_path(path: Path) -> str:
    return _sanitize_identifier(path.name.split(".")[0])


def _binding_size(layout: dict[str, Any] | None) -> int:
    if not isinstance(layout, dict):
        return 0
    binding = layout.get("binding")
    if not isinstance(binding, dict):
        return 0
    size = binding.get("size")
    return int(size) if isinstance(size, int) else 0


def _field_span_size(fields: list[Any]) -> int:
    size = 0
    for field in fields:
        if not isinstance(field, dict):
            continue
        binding = field.get("binding")
        if not isinstance(binding, dict):
            continue
        offset = binding.get("offset")
        field_size = binding.get("size")
        if isinstance(offset, int) and isinstance(field_size, int):
            size = max(size, offset + field_size)
    return size


def _extract_push_constant_size(reflection: dict[str, Any]) -> int:
    result = 0
    for parameter in reflection.get("parameters", []):
        if not isinstance(parameter, dict):
            continue

        binding = parameter.get("binding")
        binding_kind = binding.get("kind") if isinstance(binding, dict) else None
        if binding_kind != "pushConstantBuffer" and parameter.get("name") != "g_push":
            continue

        parameter_type = parameter.get("type")
        if not isinstance(parameter_type, dict):
            continue

        result = max(result, _binding_size(parameter_type.get("elementVarLayout")))

        element_type = parameter_type.get("elementType")
        if isinstance(element_type, dict):
            fields = element_type.get("fields")
            if isinstance(fields, list):
                result = max(result, _field_span_size(fields))

    return result


def _extract_thread_group_size(reflection: dict[str, Any]) -> tuple[int, int, int] | None:
    for entry_point in reflection.get("entryPoints", []):
        if not isinstance(entry_point, dict):
            continue
        if entry_point.get("stage") != "compute":
            continue
        size = entry_point.get("threadGroupSize")
        if isinstance(size, list) and len(size) == 3 and all(isinstance(value, int) for value in size):
            return int(size[0]), int(size[1]), int(size[2])
    return None


def _load_reflection(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict):
        raise ValueError(f"Reflection root must be an object: {path}")
    return data


def _render_header(shader_metadata: dict[str, dict[str, Any]]) -> str:
    lines = [
        "// =============================================================================",
        "// WestEngine - Generated Shader Metadata",
        "// Generated from Slang reflection JSON. DO NOT EDIT.",
        "// =============================================================================",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace west::shader::metadata",
        "{",
        "",
    ]

    for shader_name in sorted(shader_metadata):
        metadata = shader_metadata[shader_name]
        push_constant_size = int(metadata.get("pushConstantSizeBytes", 0))
        thread_group_size = metadata.get("threadGroupSize")

        lines.append(f"namespace {shader_name}")
        lines.append("{")
        lines.append(f"inline constexpr uint32_t PushConstantSizeBytes = {push_constant_size}u;")
        if thread_group_size is not None:
            x, y, z = thread_group_size
            lines.append(f"inline constexpr uint32_t WorkgroupSizeX = {x}u;")
            lines.append(f"inline constexpr uint32_t WorkgroupSizeY = {y}u;")
            lines.append(f"inline constexpr uint32_t WorkgroupSizeZ = {z}u;")
        lines.append(f"}} // namespace {shader_name}")
        lines.append("")

    lines.append("} // namespace west::shader::metadata")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract WestEngine shader metadata from Slang reflection JSON.")
    parser.add_argument("--output", required=True, type=Path, help="Generated ShaderMetadata.h path.")
    parser.add_argument("reflection_json", nargs="+", type=Path, help="Slang reflection JSON files.")
    args = parser.parse_args()

    shader_metadata: dict[str, dict[str, Any]] = {}

    for reflection_path in args.reflection_json:
        reflection = _load_reflection(reflection_path)
        shader_name = _shader_name_from_reflection_path(reflection_path)
        metadata = shader_metadata.setdefault(shader_name, {"pushConstantSizeBytes": 0, "threadGroupSize": None})

        metadata["pushConstantSizeBytes"] = max(
            int(metadata["pushConstantSizeBytes"]),
            _extract_push_constant_size(reflection),
        )

        thread_group_size = _extract_thread_group_size(reflection)
        if thread_group_size is not None:
            metadata["threadGroupSize"] = thread_group_size

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(_render_header(shader_metadata), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

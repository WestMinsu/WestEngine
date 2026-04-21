#!/usr/bin/env python3
# =============================================================================
# WestEngine Tests - Shader
# Slang reflection metadata extraction
# =============================================================================

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_extract_metadata.py <repo-root>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    tool = repo_root / "tools" / "extract_metadata.py"

    with tempfile.TemporaryDirectory() as temp_dir_text:
        temp_dir = Path(temp_dir_text)

        textured_reflection = temp_dir / "TexturedQuad.ps.spv.reflection.json"
        textured_reflection.write_text(
            json.dumps(
                {
                    "parameters": [
                        {
                            "name": "g_push",
                            "binding": {"kind": "pushConstantBuffer"},
                            "type": {
                                "elementVarLayout": {"binding": {"size": 16}},
                                "elementType": {
                                    "fields": [
                                        {"binding": {"offset": 0, "size": 8}},
                                        {"binding": {"offset": 8, "size": 8}},
                                    ]
                                },
                            },
                        }
                    ],
                    "entryPoints": [{"name": "PSMain", "stage": "fragment"}],
                }
            ),
            encoding="utf-8",
        )

        compute_reflection = temp_dir / "ComputeProbe.cs.spv.reflection.json"
        compute_reflection.write_text(
            json.dumps(
                {
                    "parameters": [],
                    "entryPoints": [
                        {
                            "name": "CSMain",
                            "stage": "compute",
                            "threadGroupSize": [8, 4, 2],
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

        output = temp_dir / "ShaderMetadata.h"
        subprocess.run(
            [
                sys.executable,
                str(tool),
                "--output",
                str(output),
                str(textured_reflection),
                str(compute_reflection),
            ],
            check=True,
        )

        header = output.read_text(encoding="utf-8")
        assert "Generated from Slang reflection JSON. DO NOT EDIT." in header
        assert "namespace west::shader::metadata" in header
        assert "namespace TexturedQuad" in header
        assert "inline constexpr uint32_t PushConstantSizeBytes = 16u;" in header
        assert "namespace ComputeProbe" in header
        assert "inline constexpr uint32_t WorkgroupSizeX = 8u;" in header
        assert "inline constexpr uint32_t WorkgroupSizeY = 4u;" in header
        assert "inline constexpr uint32_t WorkgroupSizeZ = 2u;" in header

    print("extract_metadata tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

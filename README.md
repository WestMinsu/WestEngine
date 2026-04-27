<h1 align="center">WestEngine</h1>

<p align="center">
  <b>DX12 / Vulkan 1.3 Dual-Backend Rendering Engine</b><br>
  Bindless RHI · Render Graph · GPU-Driven Rendering · Deferred PBR Pipeline
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square" alt="C++20">
  <img src="https://img.shields.io/badge/DirectX-12-green?style=flat-square" alt="DX12">
  <img src="https://img.shields.io/badge/Vulkan-1.3-red?style=flat-square" alt="Vulkan 1.3">
  <img src="https://img.shields.io/badge/Shader-Slang-orange?style=flat-square" alt="Slang">
</p>

---

## 프로젝트 소개

WestEngine은 **DX12와 Vulkan 1.3 듀얼 백엔드**를 하나의 RHI(Rendering Hardware Interface)로 추상화한 렌더링 엔진입니다.

GPU 리소스 수명 관리, Render Graph 기반 프레임 실행, Bindless Descriptor Model, 2.84M triangles 규모 대형 Static Scene의 GPU-Driven 렌더링을 직접 설계하고 구현하였습니다.

크로스 플랫폼을 고려하여 DXIL/SPIR-V 외에 Metal Shading Language로의 크로스 컴파일을 지원하는 구조입니다. RHI에 `MetalDevice` 구현체만 추가하면 셰이더 파이프라인은 변경 없이 작동하도록 설계하였습니다.

> **Demo Scene:** Amazon Lumberyard Bistro (약 2.84M triangles) — DX12/Vulkan 동일 코드 경로로 실행

https://casual-effects.com/data/ 의 Amazon Lumberyard Bistro 모델을 사용하였습니다.

---

## 핵심 기술 요약

| 영역 | 구현 내용 |
|---|---|
| **RHI 추상화** | `IRHIDevice`, `IRHICommandList`, `IRHIFence` 등 15개 인터페이스. 상위 코드에 DX12/Vulkan 타입이 노출되지 않음 |
| **Bindless 리소스 모델** | 엔진 전체가 **1개의 Global Root Signature / Descriptor Set Layout**을 공유. `BindlessIndex`로 리소스 접근 |
| **Render Graph** | Pass/Resource 의존성 선언 → 컴파일러가 barrier/transition 자동 해결. Transient resource aliasing 지원 |
| **GPU-Driven Rendering** | Compute Culling → `DrawIndexedIndirectCount` / `ExecuteIndirect`로 CPU draw call 제거 |
| **Deferred PBR Pipeline** | GBuffer → Shadow → SSAO → Deferred Lighting (PBR + IBL) → Bokeh DOF → Tone Mapping |
| **동기화** | Timeline Semaphore (`ID3D12Fence` / `VkTimelineSemaphore`) 기반 Triple Buffering + Fence-aware Deferred Deletion |
| **셰이더 파이프라인** | Slang 오프라인 컴파일 → DXIL + SPIR-V 동시 생성. CMake depfile 기반 증분 빌드 |
| **메모리 관리** | D3D12MA / VMA 통합. Linear / Pool / Stack 커스텀 할당기.|
| **텔레메트리** | Dear ImGui 기반 런타임 패널: GPU pass timing, RenderGraph 통계, GPU-driven draw count |

---

## 아키텍처

```
 Application / Win32 Runtime
          │
          ▼
  ┌─ west_rhi_interface ─────────────────────────────┐
  │  IRHIDevice · IRHICommandList · IRHIFence         │
  │  IRHISwapChain · IRHIPipeline · IRHIBuffer        │
  │  BindlessIndex (Resource / Sampler namespace)     │
  └──────────┬────────────────────────┬───────────────┘
             │                        │
             ▼                        ▼
     ┌─ DX12 Backend ──┐     ┌─ Vulkan 1.3 Backend ──┐
     │  D3D12MA        │     │  VMA                  │
     │  Descriptor Heap│     │  Descriptor Buffer    │
     │  D3D12 Fence    │     │  Timeline Semaphore   │
     │  Enhanced       │     │  Pipeline Barrier2    │
     │  Barriers       │     │                       │
     └────────┬────────┘     └────────┬──────────────┘
              │                       │
              └───────────┬───────────┘
                          ▼
              ┌─ Render Graph ────────────────────┐
              │  RenderGraphCompiler              │
              │  TransientResourcePool (aliasing) │
              │  CommandListPool                  │
              └────────────┬──────────────────────┘
                           ▼
    ┌──────────────────────────────────────────────┐
    │            Rendering Passes                  │
    │                                              │
    │  GPUDrivenCulling ──► GBuffer                │
    │                        │                     │
    │  ShadowMap ────────────┤                     │
    │                        ▼                     │
    │  SSAO ──────────► DeferredLighting           │
    │                        │                     │
    │                   BokehDOF                   │
    │                        │                     │
    │                   ToneMapping                │
    │                        │                     │
    │                   ImGui Overlay              │
    └──────────────────────────────────────────────┘
```
---

## 렌더링 파이프라인 상세

### 1. GPU-Driven Rendering

```
Scene Draw Records (GPU Buffer)
        │
        ▼
  ┌─ Compute Culling Pass ──┐
  │  Frustum Culling (AABB) │
  │  atomicAdd → visible ID │
  └────────┬────────────────┘
           ▼
  Indirect Arguments Buffer
  + Draw Count Buffer
           │
           ▼
  DrawIndexedIndirectCount (Vulkan)
  ExecuteIndirect (DX12)
```

- Bistro 기준: **22,396개 mesh/instance → 128개 merged draw unit**으로 압축
- GBuffer pass가 shared vertex/index buffer 1회 바인딩 후 indirect draw로 전체 씬 제출
- CPU draw loop 대비 submission collapse 달성

### 2. Deferred PBR Shading

| Pass | 출력 | 비고 |
|---|---|---|
| **GBuffer** | WorldPos · Normal · Albedo · Metallic/Roughness | Alpha discard (foliage/glass) 지원 |
| **ShadowMap** | Depth (directional) | 16-tap Rotated Poisson Disk PCF |
| **SSAO** | R16_FLOAT AO map | World-space position + normal 기반 |
| **DeferredLighting** | HDR scene color | Cook-Torrance BRDF + Fdez-Aguera multi-scattering 보정 |
| **IBL** | Diffuse irradiance + Specular GGX prefiltered cubemap + BRDF LUT | Asset-driven HDR environment |
| **BokehDOF** | DOF 적용 HDR color | Hexagonal highlight-weighted blur, screen-center autofocus |
| **ToneMapping** | LDR back buffer | ACES / Reinhard / Uncharted2 / Gran Turismo / Lottes 등 9종 |

**후처리 스택:** Chromatic Aberration → FXAA → Tone Mapping → Color Grading (Contrast/Brightness/Saturation/Vibrance) → Vignette → Film Grain

### 3. Bindless RHI 모델

```
┌──────────────────────────────────────────────────────┐
│              Global Bindless Heap                    │
│                                                      │
│  BindlessIndex(0)   ── Scene Vertex Buffer (SRV)     │
│  BindlessIndex(1)   ── Scene Index Buffer (SRV)      │
│  BindlessIndex(2)   ── Material Buffer (SRV)         │
│  BindlessIndex(3~N) ── Material Textures (SRV)       │
│  ...                                                 │
│                                                      │
│  DX12: CBV/SRV/UAV Descriptor Heap                   │
│  Vulkan: VK_EXT_descriptor_buffer                    │
└──────────────────────────────────────────────────────┘
```

- Per-pass descriptor set 교체 없이 **push constant로 BindlessIndex만 전달**
- Vulkan의 `VK_KHR_buffer_device_address` 활용
- 셰이더 코드가 API-agnostic하게 리소스 접근

### 4. Render Graph

- **자동 Barrier 해결**: Pass가 리소스 사용 의도(Read/Write/RenderTarget)만 선언하면 컴파일러가 최적 state transition 삽입
- **DX12 Enhanced Barriers** (`ID3D12GraphicsCommandList7::Barrier`) 지원 + legacy fallback
- **Vulkan `vkCmdPipelineBarrier2`** 에 narrower stage mask 전달
- **Transient Resource Aliasing**: G-Buffer 등 일시적 리소스의 VRAM 블록 재사용 (Aliasing Barrier)
- **런타임 통계**: Pass count, resource count, queue batch count, barrier count를 telemetry에 노출

### 5. 동기화 & 리소스 수명

- **Triple Buffering** + Timeline Fence/Semaphore 기반 Frame-in-Flight 동기화
- **Deferred Deletion**: GPU가 참조 중인 리소스를 fence 완료 전까지 삭제 지연
  - Bindless index unregister, transient render target, PSO, backend object 모두 적용
  - TDR(GPU Hang) 방지를 위한 핵심 안전장치

---

## 셰이더 빌드 파이프라인

```
 .slang 소스
     │
     ▼  
 Slang Compiler (slangc)
     │
     ├──► .dxil  (DX12 Shader Model 6.6)
     ├──► .spv   (Vulkan SPIR-V)
     └──► .json  (Reflection metadata)
              │
              ▼
     extract_metadata.py
              │
              ▼
     generated/ShaderMetadata.h
     (Compute workgroup sizes, push constant sizes)
```

- **Slang**: 하나의 셰이더 소스로 DXIL + SPIR-V 크로스 컴파일
- Reflection은 **metadata 추출 전용** — Global Descriptor Layout은 고정이므로 descriptor set 생성에 사용하지 않음

---

## 프로젝트 구조

```
WestEngine/
├── engine/
│   ├── core/              # Logger, Assert, Timer, Types
│   │   ├── Memory/        # Linear / Pool / Stack 할당기
│   │   └── Threading/     # TaskSystem
│   ├── platform/          # IWindow, IApplication 추상화
│   │   └── win32/         # Win32 구현체 (OS 헤더 격리)
│   ├── rhi/
│   │   ├── interface/     # IRHIDevice, IRHICommandList 등 15개 인터페이스
│   │   ├── dx12/          # DX12 백엔드 (24 files)
│   │   ├── vulkan/        # Vulkan 1.3 백엔드 (25 files)
│   │   └── common/        # 공용 RHI 유틸리티
│   ├── render/
│   │   ├── RenderGraph/   # RenderGraph, Compiler, TransientResourcePool
│   │   └── Passes/        # GBuffer, Shadow, SSAO, Lighting, DOF, ToneMapping, GPUCulling
│   ├── scene/             # SceneAsset, MeshLoader (glTF/OBJ), Camera, Material
│   ├── shader/            # PSOCache, ShaderCompiler
│   └── editor/            # ImGui Renderer, Telemetry, Runtime Controls
├── shaders/               # Slang 셰이더 (11 passes + Common)
├── tools/                 # extract_metadata.py (Reflection → C++ header)
├── tests/                 # core / rhi / render / scene / shader 단위 테스트
├── generated/             # ShaderMetadata.h (빌드 시 자동 생성)
└── assets/                # Bistro, glTF canonical scene, IBL textures
```

---

## 측정 데이터

### Bistro Scene 로딩 최적화

| 측정 항목 | Before | After |
|---|---:|---:|
| Mesh/Instance 유닛 | 22,396 | **128** (merged) |
| Debug Geometry Upload | 27,153 ms | **33.72 ms** |
| Release DX12 Startup (texture cache + batch staging) | 6,

| 최적화 단계 | DX12 | Vulkan |
|---|---:|---:|
| 최적화 전 (캐시 OFF, 배치 OFF) | 31,185 ms | 31,013 ms |
| + Texture Cache + Batch Upload | **2,406 ms** | **2,223 ms** |
| + 1024px 텍스처 해상도 제한 | **1,162 ms** | **997 ms** |

### GPU-Driven Evidence

- GPU Compute Shader가 Frustum Culling을 수행하고, 결과를 Indirect Arguments Buffer에 기록
- GBuffer 패스는 128개 개별 draw call 대신 1회의 Indirect Draw로 전체 씬을 제출 (DX12 ExecuteIndirect / Vulkan DrawIndexedIndirectCount)

---

## 기술 스택

| 분류 | 기술 |
|---|---|
| **언어** | C++20, Slang (shader), Python (tooling) |
| **Graphics API** | DirectX 12 (SM 6.6), Vulkan 1.3 |
| **빌드** | CMake 3.25+, vcpkg (manifest mode), CMake Presets |
| **메모리** | D3D12 Memory Allocator (D3D12MA), Vulkan Memory Allocator (VMA) |
| **셰이더** | Slang → DXIL + SPIR-V cross-compile |
| **UI** | Dear ImGui |
| **프로파일링** | Tracy (on-demand), GPU Timestamp Query |
| **씬 임포트** | Assimp (Bistro OBJ), cgltf (canonical glTF) |
| **텍스처** | stb_image, KTX2 (HDR cubemap) |

---

## 빌드 및 실행

### 요구 사항

- Windows 10/11
- Visual Studio 2022 (v143 toolset)
- CMake 3.25+
- Vulkan SDK 1.3+
- vcpkg (자동 의존성 관리)

### 빌드

```powershell
# Configure (vcpkg manifest 자동 설치)
cmake --preset default

# Build (Release)
cmake --build build --config Release -j
```

### 실행

```powershell
# DX12 백엔드 (기본)
build\bin\Release\west_engine.exe

# Vulkan 백엔드
build\bin\Release\west_engine.exe vulkan
```

### 조작키

| 키 | 기능 |
|---|---|
| `F1` | ImGui 패널 토글 |
| `F5` | 전체 핫키 도움말 |
| `F6` / `F7` | Post-Processing 프리셋 순환 / 초기화 |
| `F8` | Bokeh DOF 토글 |
| `F9` | Tone Mapping 알고리즘 순환 |
| `F10` / `F11` | Debug View / Channel 순환 |
| `1~0`, `J/K`, `V/B`, `O/P`, `N/M` | Exposure, Contrast, Saturation, Brightness, Vibrance, Chromatic Aberration, Film Grain 조절 |
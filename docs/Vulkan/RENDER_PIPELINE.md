# Pipeline de rendu — éditeur vectoriel / photo / dessin 2D
> Stack : C++ · Vulkan · VMA · rendu temps réel high framerate

---

## Vue d'ensemble

```
┌──────────────────────────────────────────────────────────────────┐
│  DONNÉES DE SCÈNE (CPU, en mémoire)                              │
│  Scene graph · Layer stack · Transform tree                      │
└─────────────────────────┬────────────────────────────────────────┘
                          │
┌─────────────────────────▼────────────────────────────────────────┐
│  CPU FRAME LOOP                                                  │
│  Dirty detection → Frustum culling → Tessellation → Sort/batch   │
│  Uniform update · Command recording                              │
└─────────────────────────┬────────────────────────────────────────┘
                          │
┌─────────────────────────▼────────────────────────────────────────┐
│  GPU RENDER PASSES (ordre d'exécution)                           │
│  A · Stencil / clip / masques                                    │
│  B · Géométrie fill (solid, gradient, pattern)                   │
│  C · Géométrie stroke (expansion, dash, gradient)                │
│  D · Blend / transparence / composite par layer                  │
│  E · FX offscreen (shadow, blur, filters, LUT)                   │
│  F · Contenu raster (images, brush layers, texte)                │
│  G · Editor overlay (gizmos, handles, sélection) — no depth     │
│  H · UI / HUD (grille, snapping, curseur, labels)                │
└─────────────────────────┬────────────────────────────────────────┘
                          │
         ┌────────────────┴────────────────┐
         │                                 │
┌────────▼──────────┐           ┌──────────▼──────────┐
│  PICKING (async)  │           │  SYNC + SWAPCHAIN    │
│  ID pass          │           │  Timeline semaphores  │
│  CPU readback     │           │  Frames in flight     │
│  Hit resolve      │           │  Present queue        │
└───────────────────┘           └─────────────────────-┘
                          │
┌─────────────────────────▼────────────────────────────────────────┐
│  GPU MEMORY (VMA)                                                │
│  Vertex/index pool · Uniform buffers · Texture atlas             │
└─────────────────────────┬────────────────────────────────────────┘
                          │
┌─────────────────────────▼────────────────────────────────────────┐
│  OPTIMISATIONS TEMPS RÉEL                                        │
│  Dirty region · Tess cache · Indirect draw                       │
│  Bindless descriptors · Pipeline cache · Async compute           │
└──────────────────────────────────────────────────────────────────┘
```

---

## 1 — Données de scène

Données vivant en mémoire CPU, jamais envoyées telles quelles au GPU.

| Composant | Rôle | Notes |
|---|---|---|
| **Scene graph** | Arbre hiérarchique de tous les objets (path, groupe, texte, image…) | Traversal dirty-lazy ; chaque nœud connaît son parent |
| **Layer stack** | Liste ordonnée de calques avec ordre Z, blend mode et opacité | Un calque peut contenir des sous-calques (groupes) |
| **Transform tree** | Matrices TRS locales + mondiales, pivot, parent space | Recalcul propagé seulement quand dirty ; cache la matrice monde |

---

## 2 — CPU Frame Loop

Exécuté à chaque frame, avant l'enregistrement des commandes GPU.

| Étape | Rôle | Notes critiques |
|---|---|---|
| **Dirty detection** | Marque les objets modifiés via change tracking + AABB | Cœur du high framerate : seul ce qui a changé re-tesselle |
| **Frustum culling** | Test AABB viewport — éliminer les objets hors écran | Avant toute tessellation ; peut aussi utiliser un quadtree |
| **Tessellation CPU** | Bézier cubiques / NURBS → triangle strips avec LOD selon zoom | Cache par shape ID ; résolution adaptée à l'échelle caméra |
| **Sort & batch** | Tri par Z de calque, puis par blend bucket, puis instancing | Minimise les changements de pipeline state GPU |
| **Uniform update** | Mise à jour VP matrix, état de sélection, couleurs, time, params FX | Ring buffer UBO par frame-in-flight |
| **Command recording** | vkCmd* + pipeline barriers planifiés depuis le FrameGraph | Un seul VkCommandBuffer principal, soumis une fois |

---

## 3 — GPU Render Passes

Chaque passe est un **VkRenderPass distinct** dans le même `VkCommandBuffer`.
Un `vkCmdPipelineBarrier` (image layout transition) sépare chaque passe.

### Pass A — Stencil / clip / masques

Toujours en premier. Écrit dans le stencil buffer uniquement, pas de color output.

| Sous-système | Rôle |
|---|---|
| Clip path stencil | Rasterise les clip paths avec la règle winding ou even-odd |
| Layer mask write | Écrit les masques de calque dans le stencil buffer |
| Scissor / viewport | Met à jour les régions de dessin dynamiques |

> Les passes suivantes lisent le stencil pour clipper leur output.

---

### Pass B — Géométrie : fill

| Sous-système | Rôle |
|---|---|
| Solid / flat fill | Couleur unie par shape, push constant |
| Gradient fill | Linéaire, radial, conique — calculé en fragment shader |
| Pattern / image fill | Texture tuilée, bitmap, hachures vectorielles |

---

### Pass C — Géométrie : stroke

| Sous-système | Rôle |
|---|---|
| Stroke expansion | Génère la géométrie du trait : cap (butt, round, square), join (miter, round, bevel) |
| Dash array | Pattern de pointillés animables (offset dans le temps) |
| Stroke gradient | Couleur variable le long du path (gradient de stroke) |

---

### Pass D — Blend / transparence / composite par layer

| Sous-système | Rôle |
|---|---|
| Blend modes | Normal, Multiply, Screen, Overlay, Difference, Luminosity… (état de blend Vulkan) |
| Alpha composite | Opacité de calque, groupes d'isolation (isolated group) |
| Layer group flatten | Rendu du groupe dans un FBO offline, puis composite avec blend mode du groupe |

> Les blend modes non standard (ex. Difference) nécessitent un shader custom car Vulkan ne les supporte pas nativement.

---

### Pass E — FX offscreen (effets de calque)

Rendu systématiquement dans un FBO temporaire, composite ensuite dans la scène.

| Sous-système | Rôle |
|---|---|
| Shadow / glow | Drop shadow, inner shadow, outer glow — offset + spread + blur |
| Gaussian blur | Séparable H puis V, idéalement en compute shader |
| Filters & adjustments | HSL, courbes de tons, niveaux, balance, LUT 3D, seuil, inversement |

> Passe conditionnelle : activée seulement si au moins un objet visible a un FX actif.

---

### Pass F — Contenu raster

Interleaved avec B/C/D selon l'ordre Z réel des calques dans le layer stack.

| Sous-système | Rôle |
|---|---|
| Image layer | Textures RGBA avec mipmaps, import bitmap (PNG, JPEG, EXR…) |
| Brush / paint layer | Canvas raster, coups de pinceau accumulés (mode dessin) |
| Text render | Atlas de glyphes SDF ou bitmap, hinting, rendu sub-pixel |

---

### Pass G — Editor overlay

**Profondeur désactivée** — rendu toujours par-dessus le contenu, jamais clippé.

| Sous-système | Rôle |
|---|---|
| Transform gizmos | Axes de translation XY, cercle de rotation, handles d'échelle, shear |
| Path handles | Losanges Bézier on-curve / off-curve, segments de tangente, handles NURBS |
| Bounding boxes | Contours de sélection, hover highlight, multi-sélection (tiretés) |
| Anchor points | Points de courbe on-curve et off-curve, smooth/corner indicator |
| Linked objects | Pointillés de liaison, contraintes, axes de symétrie |
| Measurement / annotations | Cotes, distances, angles, guides de distribution / alignement |

---

### Pass H — UI / HUD

Dernière passe. Interface non-destructive sur le canvas.

| Sous-système | Rôle |
|---|---|
| Grid / guides | Grille de fond, repères, règles graduées |
| Snap indicators | Magnets de snap (point, bord, centre, grille, smart guides) |
| Labels / tooltips | Coordonnées flottantes, mode outil, retours visuels |

---

## 4 — Picking / hit-test

Pass asynchrone sur la **transfer queue**, indépendante du rendu principal.

| Étape | Rôle |
|---|---|
| Object ID pass | Rend chaque shape avec color = ID entier (R32UI) dans un FBO dédié |
| CPU readback | `vkCmdCopyImageToBuffer` — readback N-1 frames pour éviter un GPU stall |
| Hit resolve | Pixel sous le curseur → ID → objet, sub-shape (segment, vertex, handle) |

> Le picking pixel-perfect évite tout raycasting CPU sur les courbes.

---

## 5 — Sync + Swapchain

| Composant | Rôle |
|---|---|
| Timeline semaphores | Synchronisation fine CPU/GPU entre passes et queues |
| Frames in flight (N=2 ou 3) | Double ou triple buffering — CPU prépare N+1 pendant que GPU rend N |
| Present queue | `VK_KHR_swapchain`, présentation via `vkQueuePresentKHR` |

---

## 6 — GPU Memory (VMA)

| Pool | Contenu |
|---|---|
| Vertex / index pool | Géométrie tessellée uploadée par le Tessellation Cache |
| Uniform buffers | VP matrix, transforms, paramètres FX — ring buffer par frame |
| Texture atlas | Patterns, images importées, atlas de glyphes |

---

## 7 — Optimisations temps réel

| Technique | Impact | Déclencheur d'invalidation |
|---|---|---|
| **Dirty region** | Redraw seulement les zones modifiées (AABB) | Tout changement de shape, de sélection ou de caméra |
| **Tess cache** | Réutilise la géométrie tessellée si la shape n'a pas changé | Édition de vertex, changement de zoom de seuil LOD |
| **Indirect draw** | `VkDrawIndirectCommand` — batch GPU sans roundtrip CPU par objet | Mis à jour seulement quand le batch change |
| **Bindless descriptors** | Toutes les textures accessibles sans changer de descriptor set (Vulkan 1.2) | Mis à jour à l'ajout / suppression d'une texture |
| **Pipeline cache** | `VkPipelineCache` sérialisé sur disque — évite les freezes au lancement | Rebuild sur changement de shader ou de device |
| **Async compute** | Blur, FX, tessellation lourde sur compute queue en parallèle du rendu | Activé si compute queue disponible et charge suffisante |

---

## Notes d'architecture Vulkan

```
① Passes A→H : VkRenderPass distincts, image layout barriers entre chaque.
② Pass G (overlay) : depth/stencil test désactivé — toujours au-dessus.
③ Pass E (FX) : render offscreen vers FBO temporaire, composite vers swapchain image.
④ Pass F (raster) : interleaved avec B/C/D selon l'ordre Z réel des calques.
⑤ Picking : pass séparée sur transfer queue, readback async N-1 frames.
⑥ Tess cache invalidé sur changement de shape, zoom seuil, ou édition de vertex.
⑦ Blend modes non-natifs Vulkan (Difference, Hue…) : shader custom obligatoire.
⑧ Layer group flatten : FBO temporaire alloué par groupe, libéré après composite.
```

---

## Architecture des dossiers

```
src/
├── core/
│   ├── Application.h/cpp          # Entry point, event loop
│   ├── Window.h/cpp               # OS window (SDL2 / GLFW)
│   └── Config.h                   # Feature flags, constantes
│
├── scene/
│   ├── SceneGraph.h/cpp           # Arbre hiérarchique, traversal
│   ├── Layer.h/cpp                # Layer stack, blend mode, masque
│   ├── Node.h/cpp                 # Nœud de base (id, transform)
│   ├── Shape.h/cpp                # Path, rect, ellipse, texte, image
│   ├── TransformComponent.h/cpp   # TRS, pivot, dirty flag
│   └── BoundingBox.h/cpp          # AABB, propagation dirty
│
├── geometry/
│   ├── Bezier.h/cpp               # Éval / tessellation Bézier cubique
│   ├── Nurbs.h/cpp                # NURBS, knot vectors
│   ├── Tessellator.h/cpp          # Courbe → triangle mesh + LOD
│   ├── StrokeBuilder.h/cpp        # Expansion stroke, cap, join, dash
│   └── TessCache.h/cpp            # Cache géométrie par shape ID
│
├── renderer/
│   ├── RenderSystem.h/cpp         # Orchestrateur, frame loop CPU
│   ├── FrameGraph.h/cpp           # Déclaration passes + dépendances
│   │
│   ├── passes/
│   │   ├── StencilPass.h/cpp      # Pass A — clip paths, layer masks
│   │   ├── FillPass.h/cpp         # Pass B — solid, gradient, pattern
│   │   ├── StrokePass.h/cpp       # Pass C — stroke expansion, dash
│   │   ├── BlendPass.h/cpp        # Pass D — blend modes, alpha, group
│   │   ├── FXPass.h/cpp           # Pass E — shadow, blur, filters, LUT
│   │   ├── RasterPass.h/cpp       # Pass F — images, brush, texte
│   │   ├── OverlayPass.h/cpp      # Pass G — gizmos, handles, sélection
│   │   └── UIPass.h/cpp           # Pass H — grille, snapping, HUD
│   │
│   ├── pipelines/
│   │   ├── FillPipeline.h/cpp     # PSO fill variants
│   │   ├── StrokePipeline.h/cpp   # PSO stroke
│   │   ├── BlendPipeline.h/cpp    # PSO blend custom (modes non-natifs)
│   │   ├── FXPipeline.h/cpp       # PSO FX (blur, shadow, LUT)
│   │   ├── OverlayPipeline.h/cpp  # PSO overlay (no depth test)
│   │   └── PipelineCache.h/cpp    # VkPipelineCache, variants
│   │
│   ├── resources/
│   │   ├── GpuBuffer.h/cpp        # Abstraction VkBuffer + VMA
│   │   ├── GpuImage.h/cpp         # Abstraction VkImage + VMA
│   │   ├── VertexPool.h/cpp       # Pool géométrie tessellée
│   │   ├── UniformBuffer.h/cpp    # Ring buffer UBO par frame
│   │   └── TextureAtlas.h/cpp     # Atlas patterns, images, glyphes
│   │
│   └── picking/
│       ├── PickingPass.h/cpp      # Rendu ID offscreen (R32UI)
│       └── HitResolver.h/cpp      # Readback async N-1, ID → objet
│
├── vulkan/
│   ├── VulkanContext.h/cpp        # Instance, device, queues
│   ├── Swapchain.h/cpp            # Swapchain, frames in flight
│   ├── CommandPool.h/cpp          # Pool + alloc command buffers
│   ├── Synchronization.h/cpp      # Semaphores, fences, timeline
│   ├── RenderPass.h/cpp           # VkRenderPass builder
│   ├── Descriptors.h/cpp          # DescriptorSet layout + pool
│   └── ShaderModule.h/cpp         # Chargement SPIR-V
│
├── editor/
│   ├── EditorState.h/cpp          # Sélection, mode outil actif
│   ├── GizmoSystem.h/cpp          # Logique gizmos transform
│   ├── HandleSystem.h/cpp         # Handles Bézier / NURBS / vertex
│   ├── SelectionRenderer.h/cpp    # Contours sélection, multi-select
│   ├── LinkedObjects.h/cpp        # Liaisons, contraintes, symétrie
│   ├── MeasurementOverlay.h/cpp   # Cotes, distances, angles
│   ├── SnapSystem.h/cpp           # Grille, point, bord, centre, smart
│   └── Cursor.h/cpp               # Curseur selon mode / contexte
│
├── effects/
│   ├── ShadowEffect.h/cpp         # Drop / inner shadow
│   ├── GlowEffect.h/cpp           # Outer / inner glow
│   ├── BlurEffect.h/cpp           # Gaussian, motion, radial blur
│   ├── ColorAdjustment.h/cpp      # HSL, courbes, niveaux, balance
│   └── LutEffect.h/cpp            # Application de LUT 3D
│
├── tools/
│   ├── Tool.h                     # Interface outil de base
│   ├── SelectTool.h/cpp
│   ├── PenTool.h/cpp              # Dessin Bézier interactif
│   ├── NodeTool.h/cpp             # Édition vertex / handle
│   ├── BrushTool.h/cpp            # Pinceau raster
│   ├── ShapeTool.h/cpp            # Rect, ellipse, polygone…
│   └── TransformTool.h/cpp        # Translate, rotate, scale, shear
│
├── text/
│   ├── FontManager.h/cpp          # Chargement FreeType / HarfBuzz
│   ├── GlyphAtlas.h/cpp           # Atlas SDF ou bitmap
│   └── TextLayout.h/cpp           # Layout, shaping, line-break
│
├── shaders/                       # Sources GLSL — compilés SPIR-V offline
│   ├── fill.vert / .frag
│   ├── gradient.frag
│   ├── pattern.frag
│   ├── stroke.vert / .frag
│   ├── blend_custom.frag          # Modes non-natifs Vulkan
│   ├── blur.comp                  # Compute shader séparable
│   ├── shadow.comp
│   ├── lut.frag
│   ├── overlay.vert / .frag
│   ├── picking.vert / .frag
│   └── grid.vert / .frag
│
└── utils/
    ├── Math.h                     # Vec2/3/4, Mat3/4, intersections
    ├── AABB.h
    ├── IdManager.h/cpp            # Génération IDs stables
    ├── RingBuffer.h               # Buffer circulaire générique
    └── Profiler.h/cpp             # GPU timestamps, CPU timers
```

---

## Règles d'architecture clés

- **`vulkan/`** ne connaît rien de la scène — wrapping bas niveau uniquement.
- **`editor/`** ne touche jamais directement à Vulkan — passe par `RenderSystem`.
- **`FrameGraph`** déclare les dépendances entre passes de façon déclarative ; les barriers sont inférées automatiquement.
- **Pass F (raster)** est conceptuellement interleaved avec B/C/D selon l'ordre Z, mais techniquement groupée en une seule passe GPU pour éviter trop de transitions.
- **Shaders** compilés offline en SPIR-V via script de build, jamais à runtime.
- **Blend modes non-natifs** (Difference, Hue, Saturation, Color, Luminosity) : implémentés en fragment shader custom, pas en état de blend fixe.

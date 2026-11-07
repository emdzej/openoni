/* The environment's visibility: rays cast from the camera through the octree mark the leaves and quads
   they reach (AKiEnvironment_RayCastOctTree 0x4433f0, run by AKrEnvironment_StartFrame 0x4430f0 each
   frame), and the draw keeps to them: the environment's quads (AKiEnvironment_ComputeVis 0x444030), the
   objects, corpses, weapons, powerups, turrets and particles by the leaves they are in. A ray that
   leaves the octree, or reaches the far plane, without meeting an opaque quad lets the sky be drawn. Notes: docs/re/visibility.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The camera as 0x4433f0 takes it: its location, view and up (M3rCamera_GetViewData 0x41b4a0) and the
   frustum's corners (0x41b500): the near plane's top left, top right, bottom left, bottom right, then the
   far plane's. */
typedef struct {
    float location[3], view[3], up[3];
    float corners[8][3];
} VisCamera;

/* AKrEnvironment_LevelBegin 0x43ffe0's part: the vectors for the level's environment (NULL: none) and the
   rays' pattern (AKrRayCastCoefficients_Initialize 0x442f50, as many a side as the graphics quality's
   ONrMotoko_GraphicsQuality_RayCount 0x5084e0). */
void vis_level_begin(const void *env);
/* AKrEnvironment_StartFrame 0x4430f0: the frame's rays (unless the camera has kept still for more than
   25 frames), their marks aged every 12th pass; whether the sky is to be drawn (a ray got out within the
   last 25 frames). */
bool vis_update(const VisCamera *cam);
/* The pass counter's step in AKiEnvironment_ComputeVis 0x444030 (the environment's draw), which shifts the
   next frame's jitter. */
void vis_env_drawn(void);

/* The quad's mark (the vector AKrEnvironment_GetVisVector 0x444010 gives the decals). No environment: true. */
bool vis_quad(uint32_t quad);
/* AKrEnvironment_PointIsVisible 0x440dc0: the leaf holding p is marked. */
bool vis_point(const float p[3]);

/* A list of the leaves a box is in (AKrEnvironment_NodeList_Get 0x440900, up to max - 1 of them; past
   that, or with none of the octree's, it stands for any), and whether one of them is marked
   (AKrEnvironment_NodeList_Visible 0x440b50). */
enum { VIS_LIST_MAX = 0x20 };
typedef struct {
    uint32_t count;             /* 0xffffffff: too many (always visible) */
    uint32_t leaf[VIS_LIST_MAX];
} VisList;
void vis_list(const float box[6], uint32_t max, VisList *l);
bool vis_list_visible(const VisList *l);
/* AKrEnvironment_IsBoundingBoxMinMaxVisible 0x440bb0 (0x440c20 from the root): one of the leaves the box
   (min x, y, z, max x, y, z) is in is marked. */
bool vis_box(const float box[6]);

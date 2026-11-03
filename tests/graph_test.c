/* The pathfinding graph (OPENONI_DATA=<dir>, default ./game; skipped without it): level 1's rooms and
   their links (PHrBuildGraph, the blockages from the rooms' grids), the room under Konoko's start (AKrNodeFromPoint), and a path across the
   level whose links lead from room to room (the rooms' A*). */
#include "akev.h"
#include "exe.h"
#include "graph.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("graph_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err) ||
        !tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "graph_test: %s\n", err);
        return 1;
    }
    const void *env = NULL;
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    int failed = 0;
    if (!env || !graph_build(env)) {
        fprintf(stderr, "graph_test: no graph\n");
        return 1;
    }
    int nr = graph_room_count(), nl = graph_link_count(), stairs = 0, back = 0;
    for (int i = 0; i < nl; i++) {
        const GraphLink *l = graph_link(i);
        if ((l->flags & 0xe) == 0xe) stairs++;
        /* each link has one the other way, through the same quad */
        for (int k = 0; k < graph_out_count(l->to); k++) {
            const GraphLink *m = graph_link(graph_out(l->to, k));
            if (m->to == l->from && m->quad == l->quad) { back++; break; }
        }
    }
    int blocked = 0;
    for (int i = 0; i < nl; i++) blocked += graph_link(i)->flags & 1;
    printf("rooms %d, links %d (%d by stairs, %d blocked), %d with a way back\n", nr, nl, stairs, blocked, back);
    if (nr != 240 || nl < nr || back != nl || !blocked) failed |= 1;
    /* the training room is apart from the warehouse (Konoko is moved there after it); in the warehouse,
       the closed doors block their links (their grids' obstacle cells all across) */
    const float t[3] = { -1101.0f, -339.0f, -126.3f }, b[3] = { 48.0f, 1.0f, 10.0f };
    int rt = graph_room_at(t), rb = graph_room_at(b);
    printf("rooms at the training floor %d, at the warehouse start %d\n", rt, rb);
    if (rt < 0 || rb < 0) failed |= 2;
    /* from the warehouse start to the farthest room it reaches, at its floor's middle */
    int seen[1024] = { 0 }, stack[1024], sp = 0, ra = rb;
    float far = 0.0f, a[3] = { b[0], b[1], b[2] };
    stack[sp++] = rb, seen[rb] = 1;
    while (sp && nr <= 1024) {
        int r = stack[--sp];
        float box[6];
        graph_room_box(r, box);
        float c[3] = { (box[0] + box[3]) * 0.5f, box[1] + 2.0f, (box[2] + box[5]) * 0.5f };
        float d = hypotf(c[0] - b[0], c[2] - b[2]);
        if (d > far && graph_room_at(c) == r) far = d, ra = r, a[0] = c[0], a[1] = c[1], a[2] = c[2];
        for (int k = 0; k < graph_out_count(r); k++) {
            const GraphLink *l = graph_link(graph_out(r, k));
            if (l->flags & 1) continue;     /* blocked: closed doors */
            int to = l->to;
            if (!seen[to]) seen[to] = 1, stack[sp++] = to;
        }
    }
    printf("farthest room %d, %.0f away at (%.1f %.1f %.1f)\n", ra, far, a[0], a[1], a[2]);
    int path[GRAPH_PATH_MAX];
    int n = graph_find_path(rb, b, ra, a, true, path);
    printf("path: %d rooms:", n);
    int at = rb;
    for (int k = 0; k + 1 < n; k++) {
        const GraphLink *l = graph_link(path[k]);
        printf(" %d", l->to);
        if (l->from != at) failed |= 4;
        at = l->to;
    }
    printf("\n");
    if (ra == rb || n < 2 || at != ra) failed |= 4;
    graph_free();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("graph_test: %s (%d)\n", failed ? "FAILED" : "ok", failed);
    return failed != 0;
}

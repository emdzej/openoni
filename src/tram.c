/* TRAM animation: see tram.h. */
#include "tram.h"

#include "tm.h"

#include <math.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

void tram_info(const void *tram, TramInfo *o)
{
    const uint8_t *t = tram;
    o->fps = rd16(t + TRAM_FPS);
    o->rotation_size = rd16(t + TRAM_ROTATION_SIZE);
    o->type = rd16(t + TRAM_TYPE);
    o->aim_type = rd16(t + TRAM_AIM_TYPE);
    o->from_state = rd16(t + TRAM_FROM_STATE);
    o->to_state = rd16(t + TRAM_TO_STATE);
    o->bones = rd16(t + TRAM_BONES);
    o->frames = rd16(t + TRAM_FRAMES);
    o->duration = rd16(t + TRAM_DURATION);
    o->varient = rd16(t + TRAM_VARIENT);
    o->flags = rd32(t + TRAM_FLAGS);
}

/* A .raw block of the animation: base + the field's offset, with len bytes inside the file. */
static const uint8_t *block(const void *tram, unsigned field, size_t at, size_t len)
{
    size_t size;
    const uint8_t *raw = tm_raw(tram, &size);
    uint32_t off = rd32((const uint8_t *)tram + field);
    if (!raw || !off || off + at + len > size) return NULL;
    return raw + off + at;
}

/* 0x437050. The x87 keeps the sines and cosines of x and z at full precision and rounds the products it
   stores to floats; y enters as 2 pi less the half angle (the constants at 0x529548 and 0x529560). */
void tram_euler_to_quat(float x, float y, float z, float q[4])
{
    const float half = 0.5f;
    float ya = 6.2831854820251465f - y * half;
    double cz = cos((double)(z * half)), sz = sin((double)(z * half));
    double cx = cos((double)(x * half)), sx = sin((double)(x * half));
    float cy = (float)cos((double)ya), sy = (float)sin((double)ya);
    float a = (float)cx * (float)cz;
    float b = (float)(sx * (float)cz);
    float c = (float)(sz * (float)cx);
    q[0] = b * cy - c * sy;
    q[1] = (float)(-(sx * sz * cy) - (double)a * sy);
    q[2] = c * cy - b * sy;
    q[3] = (float)-((double)a * cy + sx * sz * sy);
}

void tram_quat_lerp(const float a[4], const float b[4], float t, float o[4])
{
    float s = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + b[3] * a[3] >= 0.0f ? 1.0f : -1.0f;
    float u = (float)(1.0 - (double)t);
    o[0] = s * b[0] * t + a[0] * u;
    o[1] = s * b[1] * t + a[1] * u;
    o[2] = s * b[2] * t + a[2] * u;
    o[3] = a[3] * u + s * b[3] * t;
    float n = o[3] * o[3] + o[2] * o[2] + o[1] * o[1] + o[0] * o[0];
    if (n > 0.0f) {
        float k = 1.0f / sqrtf(n);
        for (int i = 0; i < 4; i++) o[i] *= k;
    } else {
        o[0] = o[1] = o[2] = 0.0f;
        o[3] = 1.0f;
    }
}

static bool decode_key(const uint8_t *k, unsigned size, float q[4])
{
    switch (size) {
    case 6:     /* three u16 angles, 2 pi / 65535 each (0x5297f0) */
        tram_euler_to_quat((float)rd16(k) * 9.5875264e-05f, (float)rd16(k + 2) * 9.5875264e-05f,
                           (float)rd16(k + 4) * 9.5875264e-05f, q);
        return true;
    case 16:    /* the quaternion as floats (0x427d30 copies it) */
        for (int i = 0; i < 4; i++) q[i] = rdf(k + 4 * i);
        return true;
    case 8:     /* s16 quaternion, 1/32767 (0x5297b8) */
        for (int i = 0; i < 4; i++) q[i] = (float)(int16_t)rd16(k + 2 * i) * 3.0518509e-05f;
        return true;
    case 4:     /* s8 quaternion, 1/127 (0x5297b0) */
        for (int i = 0; i < 4; i++) q[i] = (float)(int8_t)k[i] * 0.0078740157f;
        return true;
    default:    /* 3: angles through the exe's byte sine tables; not on the CD */
        return false;
    }
}

/* 0x427820. Each bone's stream (at the u16 offset of its entry in the table that starts the block) is
   keys of `size` bytes, each but the last followed by a byte: the frames to the next key. The walk adds
   them up until it reaches the frame, then takes the key before, the key after, or a lerp of the two. */
bool tram_rotations(const void *tram, uint32_t frame, float *quats)
{
    TramInfo in;
    tram_info(tram, &in);
    size_t size;
    const uint8_t *raw = tm_raw(tram, &size);
    uint32_t off = rd32((const uint8_t *)tram + TRAM_ROTATIONS);
    if (!raw || !off || off + 2u * in.bones > size) return false;
    const uint8_t *base = raw + off, *end = raw + size;
    unsigned ks = in.rotation_size;
    for (unsigned b = 0; b < in.bones; b++) {
        const uint8_t *p = base + rd16(base + 2 * b), *key, *next;
        int sum = 0, dur;
        do {
            if (p + 2 * ks + 1 > end) return false;
            key = p;
            dur = p[ks];
            sum += dur;
            next = p + ks + 1;
            p = next;
        } while ((int)frame > sum);
        int t = (int)frame - sum + dur;
        float q0[4], q1[4], *o = quats + 4 * b;
        if (!decode_key(key, ks, q0) || !decode_key(next, ks, q1)) return false;
        if (t == 0) memcpy(o, q0, sizeof q0);
        else if (t == dur) memcpy(o, q1, sizeof q1);
        else tram_quat_lerp(q0, q1, (float)t / (float)dur, o);
    }
    return true;
}

bool tram_velocity(const void *tram, uint32_t frame, float v[2])
{
    const uint8_t *p = block(tram, TRAM_VELOCITIES, (size_t)frame * 8, 8);
    if (!p) return false;
    v[0] = rdf(p);
    v[1] = rdf(p + 4);
    return true;
}

bool tram_height(const void *tram, uint32_t frame, float *h)
{
    const uint8_t *p = block(tram, TRAM_HEIGHTS, (size_t)frame * 4, 4);
    if (!p) return false;
    *h = rdf(p);
    return true;
}

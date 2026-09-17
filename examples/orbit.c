/*
 * A handful of bodies pulled towards a centre, stepped with a fixed timestep.
 * Small enough to read in one sitting, and it exercises the vector and scalar
 * maths the way a game or a plotter would.
 *
 * Shows: KitVec2 and KitVec3, the clamping and interpolation helpers, an arena
 * holding the whole simulation, and the timer measuring it.
 *
 *   ./examples/orbit --bodies 4 --steps 200
 */

#define KIT_IMPLEMENTATION
#include "../kit.h"

typedef struct {
    KitVec2 position;
    KitVec2 velocity;
    KitVec3 colour;          /* red, green, blue in 0..1 */
    float   mass;
} Body;

/* Gravity towards the origin, softened so that a close pass does not send a
 * body to infinity. */
static KitVec2 pull(KitVec2 position, float strength) {
    float distance = kit_vec2_len(position);
    if (distance < 0.001f) return KIT_VEC2(0, 0);

    float softened = kit_clampf(distance, 0.5f, 50.0f);
    float force    = -strength / (softened * softened);
    return kit_vec2_scale(kit_vec2_norm(position), force);
}

int main(int argc, char **argv) {
    const char *prog = kit_cli_shift(&argc, &argv);

    int  count = 3, steps = 120;
    bool help  = false;
    KitCliOpt opts[] = {
        KIT_CLI_INT ('b', "bodies", "N", "How many bodies",     &count),
        KIT_CLI_INT ('s', "steps",  "N", "How many steps",      &steps),
        KIT_CLI_FLAG('h', "help",   "Show this help",           &help),
    };
    if (!kit_cli_parse_arr(opts, &argc, &argv, NULL)) return 1;
    if (help) { kit_cli_usage_arr(stdout, prog, opts); return 0; }

    kit_log_set_fields(KIT_LOG_FIELD_LEVEL);
    count = kit_clampi(count, 1, 64);
    steps = kit_clampi(steps, 1, 100000);

    /* One arena for the whole run, sized up front so it never has to chain a
     * second region. The bodies are allocated once and released together. */
    KitArena arena  = kit_arena_make(sizeof(Body) * 64 + 256);
    Body    *bodies = kit_arena_alloc_array(&arena, Body, (size_t)count);

    /* A scratch row of positions, aligned for whatever vector unit a plotting
     * routine might want to read them with. */
    KitArenaMark before_trail = kit_arena_mark(&arena);
    KitVec2     *trail = (KitVec2 *)kit_arena_alloc_aligned(&arena, sizeof(KitVec2) * (size_t)count, 32);

    for (int i = 0; i < count; i++) {
        /* Spread the bodies over a circle, and colour them along the way. */
        float t     = (float)i / (float)count;
        float angle = KIT_DEG2RAD(t * 360.0f);
        float reach = kit_remapf(t, 0.0f, 1.0f, 4.0f, 12.0f);

        bodies[i].position = KIT_VEC2(cosf(angle) * reach, sinf(angle) * reach);
        /* Perpendicular to the radius, which is roughly a circular orbit. */
        bodies[i].velocity = kit_vec2_scale(KIT_VEC2(-sinf(angle), cosf(angle)),
                                            kit_lerpf(1.2f, 0.4f, t));
        bodies[i].colour   = kit_vec3_norm(KIT_VEC3(t, 1.0f - t, 0.5f));
        bodies[i].mass     = kit_lerpf(1.0f, 4.0f, t);
    }

    /* The bounds of the run, tracked as the simulation goes. */
    float closest = 1.0e30f, farthest = 0.0f;

    const float step_seconds = 0.01f;
    KitTimer    clock        = kit_timer_start();
    float       travelled    = 0.0f;

    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < count; i++) {
            KitVec2 before = bodies[i].position;
            KitVec2 force  = pull(bodies[i].position, 40.0f * bodies[i].mass);

            bodies[i].velocity = kit_vec2_add(bodies[i].velocity,
                                              kit_vec2_scale(force, step_seconds));
            bodies[i].position = kit_vec2_add(bodies[i].position,
                                              kit_vec2_scale(bodies[i].velocity, step_seconds));
            travelled += kit_vec2_dist(before, bodies[i].position);

            float reach = kit_vec2_len(bodies[i].position);
            closest  = KIT_MIN(closest, reach);
            farthest = KIT_MAX(farthest, reach);
            trail[i] = bodies[i].position;
        }
    }
    KIT_UNUSED(trail);

    printf("%d bodies, %d steps of %.0f ms, %.1f units travelled in %.1f ms\n",
           count, steps, (double)(step_seconds * 1000.0f), (double)travelled,
           kit_timer_ms(clock));

    for (int i = 0; i < count; i++) {
        KitVec2 p     = bodies[i].position;
        KitVec2 v     = bodies[i].velocity;
        /* How much of the velocity points outwards: a dot product against the
         * radius, normalised. */
        float   climb = kit_vec2_dot(kit_vec2_norm(p), kit_vec2_norm(v));
        KitVec3 rgb   = kit_vec3_scale(bodies[i].colour, 255.0f);

        printf("  body %d  at " KIT_VEC2_FMT "  speed %5.2f  %s  rgb(%3.0f,%3.0f,%3.0f)\n",
               i, KIT_VEC2_ARG(p), (double)kit_vec2_len(v),
               climb > 0.1f ? "climbing" : climb < -0.1f ? "falling " : "circling",
               (double)rgb.x, (double)rgb.y, (double)rgb.z);
    }

    /* The 3D helpers, on the colours, which are vectors like any other. */
    KitVec3 sum = KIT_VEC3(0, 0, 0);
    for (int i = 0; i < count; i++) sum = kit_vec3_add(sum, bodies[i].colour);
    KitVec3 average = kit_vec3_scale(sum, 1.0f / (float)count);
    KitVec3 spread  = kit_vec3_sub(bodies[count - 1].colour, bodies[0].colour);
    KitVec3 doubled = kit_vec3_mul(average, KIT_VEC3(2, 2, 2));
    KitVec3 spin    = kit_vec3_cross(KIT_VEC3(1, 0, 0), KIT_VEC3(0, 1, 0));

    printf("colours: average " KIT_VEC3_FMT " (length %.2f), spread " KIT_VEC3_FMT
           ", doubled " KIT_VEC3_FMT "\n",
           KIT_VEC3_ARG(average), (double)kit_vec3_len(average),
           KIT_VEC3_ARG(spread), KIT_VEC3_ARG(doubled));

    KitVec2 span = kit_vec2_sub(bodies[count - 1].position, bodies[0].position);
    printf("spin axis " KIT_VEC3_FMT ", half the span " KIT_VEC2_FMT
           ", %.0f degrees between the axes\n",
           KIT_VEC3_ARG(spin), KIT_VEC2_ARG(kit_vec2_mul(span, KIT_VEC2(0.5f, 0.5f))),
           (double)KIT_RAD2DEG(acosf(kit_clampf(kit_vec3_dot(KIT_VEC3(1,0,0), KIT_VEC3(0,1,0)), -1, 1))));
    printf("orbit between %.2f and %.2f units, drift %.4f (a double, clamped)\n",
           (double)closest, (double)farthest,
           kit_clampd((double)(farthest - closest), 0.0, 1000.0));

    /* The trail is thrown away, and the arena goes back to where it was. */
    kit_arena_rewind(&arena, before_trail);
    printf("arena: %zu bytes used of %zu held, %zu after dropping the trail\n",
           kit_arena_used(&arena) + sizeof(KitVec2) * (size_t)count,
           kit_arena_capacity(&arena), kit_arena_used(&arena));

    /* Reusing the arena for a second run would start here. */
    kit_arena_reset(&arena);

    kit_arena_free(&arena);
    return 0;
}

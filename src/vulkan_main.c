/*
 * SPACE GL - VULKAN CLIENT APPLICATION ENTRY POINT
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Translation Unit: vulkan_main.c
 * Role: main(), GLFW window creation, input callbacks, joystick handling,
 * and high-level tick loop orchestrating scene_update() and render_frame().
 * Peer modules: vulkan_device.c, vulkan_scene.c, vulkan_render.c.
 */

#include "vulkan_device_internal.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

void vulkan_send_ipc_command(VulkanApp *app, const char *cmd_str) {
    if (!app->shm) return;
    int tail = atomic_load_explicit(&app->shm->cmd_tail, memory_order_relaxed);
    int head = atomic_load_explicit(&app->shm->cmd_head, memory_order_acquire);
    int next_tail = (tail + 1) % CMD_QUEUE_SIZE;
    if (next_tail != head) {
        /* Bounded copy with guaranteed NUL termination (a source of
         * slot-size or longer is truncated, not silently unterminated).
         * The bounded memcpy also keeps -Wstringop-truncation quiet. */
        size_t n = strlen(cmd_str);
        if (n >= sizeof(app->shm->cmd_queue[0].cmd))
            n = sizeof(app->shm->cmd_queue[0].cmd) - 1;
        memcpy(app->shm->cmd_queue[tail].cmd, cmd_str, n);
        app->shm->cmd_queue[tail].cmd[n] = '\0';
        atomic_store_explicit(&app->shm->cmd_tail, next_tail, memory_order_release);
    }
    /* queue full: command is silently dropped (rate-limit prevents saturation) */
}


/* ------------------------------------------------------------------ */
/* js_apply_dead: apply symmetric dead zone to a stick axis.           */
/* Values within [-JS_DEAD_ZONE, +JS_DEAD_ZONE] map to 0.             */
/* The remaining range is rescaled linearly to [-1, +1].              */
/* NOTE: do NOT use this for the throttle (axis 3) — the throttle     */
/* has no mechanical center and uses a one-sided dead zone at min.    */
/* ------------------------------------------------------------------ */
static inline float js_apply_dead(float v) {
    if (v > -JS_DEAD_ZONE && v < JS_DEAD_ZONE) return 0.0f;
    float sign = (v > 0.0f) ? 1.0f : -1.0f;
    return sign * (fabsf(v) - JS_DEAD_ZONE) / (1.0f - JS_DEAD_ZONE);
}

/* ------------------------------------------------------------------ */
/* pilot_send_imp: rate-limited, change-detected 3-arg "imp" uplink   */
/* (stick: align the nose to the target course, then cruise at S).    */
/* A command is re-sent only when the rounded course/speed actually   */
/* changed (same rounding as the wire format: H/M to 0.1 deg, S to    */
/* 0.001). The pre-fix code re-sent the identical "imp" at 10 Hz while */
/* the stick sat still, and the server answered "Course plotted.      */
/* Aligning for Impulse drive." for every duplicate, flooding the     */
/* command deck (2026.10.06.07). Returns 1 if a command was queued.   */
/* ------------------------------------------------------------------ */
static int pilot_send_imp(VulkanApp *app, double h, double m, double s,
                           double now) {
    h = round(h * 10.0) / 10.0;
    m = round(m * 10.0) / 10.0;
    s = round(s * 1000.0) / 1000.0;
    if (now - app->jsLastImpTime <= JS_IMP_INTERVAL) return 0;
    if (h == app->jsLastImpH && m == app->jsLastImpM && s == app->jsLastImpS)
        return 0;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "imp %.1f %.1f %.3f", h, m, s);
    PILOT_DIAG("[PILOT DIAG] → %s\n", cmd);
    vulkan_send_ipc_command(app, cmd);
    app->pilotLastSpeed = (float)s;
    app->jsLastImpTime  = now;
    app->jsLastImpH = h; app->jsLastImpM = m; app->jsLastImpS = s;
    return 1;
}

/* ------------------------------------------------------------------ */
/* pilot_send_thr: rate-limited 1-arg "imp <S>" throttle uplink.      */
/* The 1-arg impulse sets the cruise speed and the flight vector from */
/* the ship's CURRENT nose — it never touches the attitude target, so */
/* the throttle can no longer drag the nose to a stale course (the    */
/* pre-fix 3-arg throttle re-aimed the open-loop pilotHeading and the */
/* ship spent up to a second per command aligning to the wrong course */
/* before moving — "inaccurate, only moves near full" 2026.10.07.02). */
/* reassert=1 (ship idle) bypasses the speed-dedup so the cruise is   */
/* re-engaged after a twist ("pos") or a retro release stopped it.    */
/* ------------------------------------------------------------------ */
static int pilot_send_thr(VulkanApp *app, double s, double now,
                           int reassert) {
    s = round(s * 1000.0) / 1000.0;
    if (now - app->jsLastThrTime <= JS_IMP_INTERVAL) return 0;
    if (!reassert && s == app->jsLastThrS) return 0;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "imp %.3f", s);
    PILOT_DIAG("[PILOT DIAG] → %s\n", cmd);
    vulkan_send_ipc_command(app, cmd);
    app->pilotLastSpeed = (float)s;
    app->jsLastThrTime  = now;
    app->jsLastThrS     = s;
    return 1;
}

/* ------------------------------------------------------------------ */
/* pilot_send_vec: rate-limited, change-detected "vec <H> <M> <S>"    */
/* uplink — the vector thrusters (2026.10.07.02): the ship translates */
/* along course (H, M) at speed S WITHOUT any attitude change (the    */
/* server sets the flight vector and cruises NAV_STATE_IMPULSE, the   */
/* nose stays where it points: the cockpit view never moves). S = 0   */
/* is the thrust release (all stop). force=1 bypasses the 10 Hz gate  */
/* (hat rising edge / release: must fire immediately).                */
/* ------------------------------------------------------------------ */
static int pilot_send_vec(VulkanApp *app, double h, double m, double s,
                           double now, int force) {
    h = round(h * 10.0) / 10.0;
    m = round(m * 10.0) / 10.0;
    s = round(s * 1000.0) / 1000.0;
    if (!force && now - app->jsLastVecTime <= JS_IMP_INTERVAL) return 0;
    if (h == app->jsLastVecH && m == app->jsLastVecM && s == app->jsLastVecS)
        return 0;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "vec %.1f %.1f %.3f", h, m, s);
    PILOT_DIAG("[PILOT DIAG] → %s\n", cmd);
    vulkan_send_ipc_command(app, cmd);
    app->pilotLastSpeed = (float)s;
    app->jsLastVecTime  = now;
    app->jsLastVecH = h; app->jsLastVecM = m; app->jsLastVecS = s;
    return 1;
}

/* ------------------------------------------------------------------ */
/* pilot_hat_retro: hat switch position -> VECTORED retro-thruster.   */
/*                                                                     */
/* The hat is a LATCHED control whose vector is captured ONCE on the  */
/* rising edge (the hat leaving center) from the ship's live course   */
/* h0/m0: H* = (h0 + 180) % 360 and M* = -m0 (+/-15 deg mark offset   */
/* on the diagonals) — the exact reverse of the nose, so the ship     */
/* BRAKES without turning. The 2026.10.07.01 "imp (h0+180) -m0 S"     */
/* could not do that: the 3-arg impulse ALWAYS aligns the nose to the */
/* target course, so the server rolled the ship 180 deg (the cockpit  */
/* view spun for a whole second) and only then burned — the hat read  */
/* as a camera switch and no thruster ever fired (2026.10.07.02).     */
/* The command is now the server's "vec" (vector thrust): the ship    */
/* translates along the captured vector while van_h/van_m/van_r are  */
/* untouched. Each of the 8 positions fires its own thruster:         */
/* per-position strengths UP 10 / RIGHT 40 / LEFT 80 / DOWN 100 %     */
/* (full stop), diagonals +/-15 deg mark drift (lateral correction).  */
/* While the hat stays off-center the SAME captured command is        */
/* re-asserted (the throttle branch is suppressed for the whole hold  */
/* — the brake wins against the throttle) and pilot_send_vec's        */
/* change-detection drops the duplicates: one hat action = one        */
/* "vec" (one "Vector thrust engaged." answer). When the hat returns  */
/* to center the caller sends "vec H* M* 0" (thrust released).        */
/* ------------------------------------------------------------------ */
static void pilot_hat_retro(VulkanApp *app, unsigned char hat, double now) {
    float pct = 1.00f, mark_off = 0.0f;
    if      (hat == GLFW_HAT_UP)                              pct = 0.10f;
    else if (hat == GLFW_HAT_RIGHT)                           pct = 0.40f;
    else if (hat == GLFW_HAT_LEFT)                            pct = 0.80f;
    else if (hat == GLFW_HAT_DOWN)                            pct = 1.00f;
    else if (hat == (GLFW_HAT_UP   | GLFW_HAT_RIGHT))         { pct = 0.10f; mark_off = -15.0f; }
    else if (hat == (GLFW_HAT_UP   | GLFW_HAT_LEFT))          { pct = 0.10f; mark_off = +15.0f; }
    else if (hat == (GLFW_HAT_DOWN | GLFW_HAT_RIGHT))         { pct = 0.80f; mark_off = -15.0f; }
    else if (hat == (GLFW_HAT_DOWN | GLFW_HAT_LEFT))          { pct = 0.80f; mark_off = +15.0f; }

    if (!app->jsRetroActive) {
        /* RISING EDGE: capture the live course ONCE, build the fixed  */
        /* retro vector and fire IMMEDIATELY (no 10 Hz gate latency).  */
        int r_idx = atomic_load(&app->shm->read_index);
        GameState *st = &app->shm->buffers[r_idx];
        double m_retro = -st->shm_m + (double)mark_off;
        if (m_retro >  90.0) m_retro =  90.0;
        if (m_retro < -90.0) m_retro = -90.0;
        app->jsRetroH = fmod(st->shm_h + 180.0, 360.0);
        app->jsRetroM = m_retro;
        app->jsRetroS = (double)(pct * 100.0f);
        app->jsRetroActive = 1;
        pilot_send_vec(app, app->jsRetroH, app->jsRetroM, app->jsRetroS,
                       now, 1);
        return;
    }
    /* Latched re-assertion of the CAPTURED vector: fixed for the whole */
    /* hold, so pilot_send_vec's dedup keeps it to one wire command per */
    /* hat action (a lost-queue command is re-sent on the next beat).   */
    pilot_send_vec(app, app->jsRetroH, app->jsRetroM, app->jsRetroS,
                   now, 0);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {

    (void)scancode;
    (void)mods;
    VulkanApp* app = (VulkanApp*)glfwGetWindowUserPointer(window);
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    
    if (key == GLFW_KEY_LEFT) {
        app->angleY -= 5.0f;
    }
    if (key == GLFW_KEY_RIGHT) {
        app->angleY += 5.0f;
    }
    if (key == GLFW_KEY_UP) {
        app->angleX -= 2.5f; /* Decreasing angleX to tilt camera UP */
    }
    if (key == GLFW_KEY_DOWN) {
        app->angleX += 2.5f; /* Increasing angleX to tilt camera DOWN */
    }
    if (key == GLFW_KEY_PAGE_UP) {
        app->cameraDist -= QG_CAMERA_STEP;
    }
    if (key == GLFW_KEY_PAGE_DOWN) {
        app->cameraDist += QG_CAMERA_STEP;
    }
    if (key == GLFW_KEY_M) {
        app->shm_inspector_page = (app->shm_inspector_page == 0) ? 1 : 0;
    }
    if (app->shm_inspector_page > 0) {
        if (key == GLFW_KEY_N) { app->shm_inspector_page = (app->shm_inspector_page % 3) + 1; }
        if (key == GLFW_KEY_P) { app->shm_inspector_page = (app->shm_inspector_page == 1) ? 3 : app->shm_inspector_page - 1; }
    }
    if (key == GLFW_KEY_SPACE) {
        app->autoRotate = !app->autoRotate;
    }
    
    if (app->cameraDist < 10.0f) app->cameraDist = 10.0f;
    if (app->angleX > 85.0f) app->angleX = 85.0f;
    if (app->angleX < -85.0f) app->angleX = -85.0f;
}



void pilot_process_joystick(VulkanApp *app, double now) {
    (void)now;
            if (app->showPilot && app->jsIndex >= 0) {
                int axis_count = 0, btn_count = 0;
                const float   *axes = glfwGetJoystickAxes   (app->jsIndex, &axis_count);
                const uint8_t *btns = glfwGetJoystickButtons(app->jsIndex, &btn_count);
                double now = glfwGetTime();
                /* DIAG: print joystick state once per second            */
                /* (SPACEGL_PILOT_DIAG=1) */
                {
                    static double _js_diag_t = 0;
                    if (now - _js_diag_t > 1.0) {
                        PILOT_DIAG("[PILOT DIAG] JS poll: jsIndex=%d axes=%d btns=%d  "
                                   "thr=%.3f stickX=%.3f stickY=%.3f twist=%.3f\n",
                                   app->jsIndex, axis_count, btn_count,
                                   (axes && axis_count > 3) ? axes[3] : -99.0f,
                                   (axes && axis_count > 0) ? axes[0] : -99.0f,
                                   (axes && axis_count > 1) ? axes[1] : -99.0f,
                                   (axes && axis_count > 2) ? axes[2] : -99.0f);
                        _js_diag_t = now;
                    }
                }


                /* -- Axes -- */
                if (axes && axis_count >= 4) {
                    /* Stick: symmetric dead zone */
                    float ax = js_apply_dead(axes[0]);  /* yaw   */
                    float ay = js_apply_dead(axes[1]);  /* pitch */
                    float az = (axis_count >= 3) ? js_apply_dead(axes[2]) : 0.0f; /* roll */

                    /* Throttle: one-sided dead zone at minimum */
                    float raw_thr = axes[3]; /* -1=max, +1=min */
                    float t01 = (1.0f - raw_thr) / 2.0f; /* 0..1 */
                    if (t01 < JS_DEAD_ZONE / 2.0f) t01 = 0.0f;
                    float imp_val = t01 * 100.0f; /* 0.000..100.000 */

                    /* CLOSED-LOOP REFERENCE (2026.10.07.02): every command  */
                    /* target below is computed from the ship's LIVE course  */
                    /* (SHM, written by the client at 60 Hz) — the pre-fix   */
                    /* open-loop pilotHeading/Mark/Roll accumulation ran     */
                    /* ahead of the real ship and every 1 s server ramp      */
                    /* sawtoothed behind it (stutter, mirrored yaw, stale    */
                    /* throttle re-aims).                                    */
                    int r_idx = atomic_load(&app->shm->read_index);
                    GameState *lives = &app->shm->buffers[r_idx];
                    double h0 = lives->shm_h;
                    double m0 = lives->shm_m;
                    double r0 = lives->shm_r;
                    int    nav = lives->shm_nav_state;

                    /* -- Hat switch: 8-way VECTORED retro-thruster (PoV) -- */
                    /* GLFW maps the T.16000M hat as a "joystick hat" via    */
                    /* glfwGetJoystickHats(), NOT as extra axes (axes=4 only)*/
                    /* Hat bitmask: GLFW_HAT_UP=1, RIGHT=2, DOWN=4, LEFT=8.  */
                    /* LATCHED: the retro vector is captured ONCE on the     */
                    /* rising edge (pilot_hat_retro) from the live course    */
                    /* and re-asserted (dedup drops the repeats) while the   */
                    /* hat stays off-center; the throttle and stick branches */
                    /* are suppressed for the whole hold, so the brake wins  */
                    /* against the throttle. The server "vec" command moves  */
                    /* the ship along the vector WITHOUT touching the        */
                    /* attitude — the cockpit view stays on the nose         */
                    /* (the pre-fix 3-arg imp rolled the ship 180 deg first, */
                    /* 2026.10.07.02). Release at center: "vec H M 0".       */
                    int hat_count = 0;
                    const unsigned char *hats =
                        glfwGetJoystickHats(app->jsIndex, &hat_count);
                    unsigned char hat = (hats && hat_count > 0) ? hats[0] : GLFW_HAT_CENTERED;

                    if (hat != GLFW_HAT_CENTERED) {
                        /* VECTORED RETRO-THRUSTER (latched, edge-captured). */
                        pilot_hat_retro(app, hat, now);
                    } else {
                        if (app->jsRetroActive) {
                            /* Hat returned to center: release the latch and  */
                            /* drop the thrust (vector all-stop). The next    */
                            /* actuation re-captures a fresh vector.          */
                            app->jsRetroActive = 0;
                            pilot_send_vec(app, app->jsRetroH, app->jsRetroM,
                                           0.0, now, 1);
                        }

                        if (ax != 0.0f || ay != 0.0f) {
                            /* STICK: closed-loop rate turn (2026.10.07.02). */
                            /* Target course = LIVE course + lead x          */
                            /* deflection: the server's 1 s alignment ramp    */
                            /* turns the ship at (lead x deflection) deg/s,   */
                            /* continuously while held, and settles onto the  */
                            /* last target on release. YAW SENSE: right stick */
                            /* (ax > 0) must turn the nose RIGHT — a target   */
                            /* heading BELOW the live one (the pre-fix        */
                            /* pilotHeading += ax * rate sent the nose LEFT). */
                            /* PITCH SENSE (2026.10.07.01): stick forward     */
                            /* (ay > 0) dives — the POSITIVE mark (dz =       */
                            /* sin(m) > 0, game +Z renders screen-down). The  */
                            /* speed is max(throttle, stick): full stick      */
                            /* = JS_STICK_SPEED_MAX, so the ship moves where  */
                            /* the nose points even at throttle rest (the     */
                            /* twist adds no thrust: roll is a rotation).     */
                            double th = fmod(h0 - (double)ax * JS_YAW_AHEAD, 360.0);
                            if (th < 0.0) th += 360.0;
                            double tm = m0 + (double)ay * JS_PITCH_AHEAD;
                            if (tm >  90.0) tm =  90.0;
                            if (tm < -90.0) tm = -90.0;
                            float stick_speed =
                                JS_STICK_SPEED_MAX * fmaxf(fabsf(ax), fabsf(ay));
                            pilot_send_imp(app, th, tm,
                                           (double)fmaxf(imp_val, stick_speed),
                                           now);
                        } else if (imp_val > 0.0f) {
                            /* THROTTLE: 1-arg "imp <S>" — cruise speed along */
                            /* the CURRENT nose, no attitude target (the      */
                            /* pre-fix 3-arg re-aimed the stale open-loop     */
                            /* heading: up to a 1 s align to the wrong course */
                            /* per command, no motion until the stale target  */
                            /* caught up — "inaccurate, only near full").     */
                            /* Re-assert (bypassing the speed-dedup) when the */
                            /* ship is IDLE: that re-engages the cruise after */
                            /* a twist ("pos") or a retro release stopped it. */
                            pilot_send_thr(app, (double)imp_val, now,
                                           nav == 0 /* NAV_STATE_IDLE */);
                        }
                        /* else: stick centred, throttle at rest -> no        */
                        /* command -> whatever velocity holds is kept.        */

                        /* TWIST: closed-loop roll via "pos H M R" at max     */
                        /* 5 Hz, target = live roll + lead x deflection; the  */
                        /* dedup (rounded h/m/r) drops repeats, so a held     */
                        /* twist costs one command per 200 ms of roll.       */
                        if (fabsf(az) > 0.0f &&
                            now - app->jsLastPosTime > JS_POS_INTERVAL) {
                            double tr = fmod(r0 + (double)az * JS_ROLL_AHEAD, 360.0);
                            if (tr < 0.0) tr += 360.0;
                            tr = round(tr * 10.0) / 10.0;
                            double th2 = round(h0 * 10.0) / 10.0;
                            double tm2 = round(m0 * 10.0) / 10.0;
                            if (!(th2 == app->jsLastPosH && tm2 == app->jsLastPosM &&
                                  tr == app->jsLastPosR)) {
                                char cmd[128];
                                snprintf(cmd, sizeof(cmd), "pos %.1f %.1f %.1f",
                                         th2, tm2, tr);
                                PILOT_DIAG("[PILOT DIAG] → %s\n", cmd);
                                vulkan_send_ipc_command(app, cmd);
                                app->jsLastPosTime = now;
                                app->jsLastPosH = th2; app->jsLastPosM = tm2;
                                app->jsLastPosR = tr;
                            }
                        }
                    }
                    app->jsPrevHatX = (float)hat; /* keep the byte for reset */
                }


                /* -- Buttons: edge detection (fire on rising edge only) -- */
                if (btns && btn_count > 0) {
                    int nb = btn_count < 16 ? btn_count : 16;
#define JS_RISING(b) ((b) < nb && btns[b] && !app->jsPrevBtns[b])
                    /* btn 0: Trigger → ion beam phaser (right thumb).       */
                    /* "pha <E>" (one arg): the server resolves the          */
                    /* currently locked target (handle_pha args==1) and      */
                    /* answers "No target locked." when none is held. The    */
                    /* pre-fix bare "pha" (zero args) was always rejected    */
                    /* with "Usage: pha <ID> <E>." and the beam never fired  */
                    /* (2026.10.06.07).                                      */
                    if (JS_RISING(0) && now - app->jsLastPhaTime > JS_PHA_LOCKOUT) {
                        char cmd[64];
                        snprintf(cmd, sizeof(cmd), "pha %d", PILOT_PHA_ENERGY);
                        vulkan_send_ipc_command(app, cmd);
                        app->jsLastPhaTime = now;
                    }
                    /* btn 1: Pollice alto → lock next target */
                    if (JS_RISING(1))
                        vulkan_send_ipc_command(app, "lock next");
                    /* btn 2: Pollice basso-sx → torpedo */
                    if (JS_RISING(2) && now - app->jsLastTorTime > JS_TOR_LOCKOUT) {
                        vulkan_send_ipc_command(app, "tor");
                        app->jsLastTorTime = now;
                    }
                    /* btn 3: Pollice basso-dx → release lock */
                    if (JS_RISING(3))
                        vulkan_send_ipc_command(app, "lock off");
                    /* btn 4: Base 1st → ALL STOP (immediate, no rate limit) */
                    if (JS_RISING(4)) {
                        vulkan_send_ipc_command(app, "imp 0 0 0");
                        app->pilotLastSpeed = 0.0f;
                        /* Sync EVERY change detector with this direct send:
                         * without it, re-engaging at the same rounded
                         * course/speed would be swallowed by the dedup.
                         * (2026.10.06.07; the throttle/vec/pos channels
                         * are the 2026.10.07.02 additions) */
                        app->jsLastImpH = 0.0;
                        app->jsLastImpM = 0.0;
                        app->jsLastImpS = 0.0;
                        app->jsLastImpTime = now;
                        app->jsLastThrS = 0.0;  app->jsLastThrTime = now;
                        app->jsLastVecS = 0.0;  app->jsLastVecTime = now;
                    }
                    /* btn 5: Base → power to engines */
                    if (JS_RISING(5))
                        vulkan_send_ipc_command(app, "pow 80 10 10");
                    /* btn 6: Base → power to shields */
                    if (JS_RISING(6))
                        vulkan_send_ipc_command(app, "pow 20 70 10");
                    /* btn 7: Base → power to weapons */
                    if (JS_RISING(7))
                        vulkan_send_ipc_command(app, "pow 20 10 70");
                    /* btn 8: Base → toggle cloaking */
                    if (JS_RISING(8))
                        vulkan_send_ipc_command(app, "clo");
                    /* btn 9: Base → emergency warp */
                    if (JS_RISING(9))
                        vulkan_send_ipc_command(app, "xxx");
#undef JS_RISING
                    memcpy(app->jsPrevBtns, btns, (size_t)nb);
                }
            }


}


void mainLoop(VulkanApp* app) {
    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(app->window)) {
        double currentTime = glfwGetTime();
        float deltaTime = (float)(currentTime - lastTime);
        lastTime = currentTime;

        if (app->autoRotate) {
            app->angleY += VIEW_ROTATION_SPEED * deltaTime;
            if (app->angleY >= 360.0f) app->angleY -= 360.0f;
            if (app->angleY < 0.0f) app->angleY += 360.0f;
        }

        glfwPollEvents();
        pilot_process_joystick(app, currentTime);

        scene_update(app, deltaTime);
        render_frame(app, deltaTime);

        struct timespec ts = {0, 1000000}; /* 1ms throttle */
        nanosleep(&ts, NULL);
    }
    vkDeviceWaitIdle(app->device);
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("spacegl_vulkan 2026.10.13.01\n");
            return 0;
        }
    }

    const char *no_msaa_env = getenv("SPACEGL_NO_MSAA");
    if (no_msaa_env && atoi(no_msaa_env) == 1) {
        s_no_msaa = true;
    }

    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    VulkanApp* app = malloc(sizeof(VulkanApp));
    if (!app) {
        return 1;
    }
    memset(app, 0, sizeof(VulkanApp));
    app->shm_fd = -1;

    /* Architectural switch: SPACEGL_GPD=0 (or unset) keeps the legacy
     * CPU-driven path; SPACEGL_GPD=1 selects the GPU-driven one (with
     * automatic fallback to CPU-driven if the device cannot support it). */
    sglog_init("vkv");
    sglog_apply_args(argc, argv);

    const char *gpd = getenv("SPACEGL_GPD");
    app->gdd_wanted = (gpd != NULL && atoi(gpd) == 1);
    if (app->gdd_wanted) {
        printf("[GDD] SPACEGL_GPD=1: requesting the GPU-driven path (Vulkan 1.3+)\n");
        SG_NOTICE(SG_CAT_VULKAN, "SPACEGL_GPD=1: requesting GPU-driven path (pid %d)", (int)getpid());
    } else {
        SG_NOTICE(SG_CAT_VULKAN, "CPU-driven path (SPACEGL_GPD unset/0, pid %d)", (int)getpid());
    }

    /* CLI options (2026.10.11.01): the screenshot/golden-render flags
       plus the SHM segment name (the first non-option argument, as
       before). --window must be parsed BEFORE glfwCreateWindow. */
    int win_w = VIEW_WINDOW_WIDTH, win_h = VIEW_WINDOW_HEIGHT;
    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--screenshot") == 0 && ai + 1 < argc) {
            snprintf(s_shot_path, sizeof(s_shot_path), "%s", argv[++ai]);
        } else if (strcmp(argv[ai], "--screenshot-frame") == 0 && ai + 1 < argc) {
            s_shot_frame = atoi(argv[++ai]);
            if (s_shot_frame < 1) s_shot_frame = 1;
        } else if (strcmp(argv[ai], "--seed") == 0 && ai + 1 < argc) {
            s_rng_seed = atoi(argv[++ai]);
        } else if (strcmp(argv[ai], "--no-rotate") == 0) {
            s_no_rotate = true;
        } else if (strcmp(argv[ai], "--no-msaa") == 0) {
            s_no_msaa = true;
        } else if (strcmp(argv[ai], "--window") == 0 && ai + 1 < argc) {
            int ww = 0, wh = 0;
            if (sscanf(argv[++ai], "%dx%d", &ww, &wh) == 2 && ww > 0 && wh > 0) {
                win_w = ww; win_h = wh;
            }
        } else if (strcmp(argv[ai], "--help") == 0 || strcmp(argv[ai], "-h") == 0) {
            printf("Usage: %s [SHM_NAME] [OPTIONS]\n", argv[0]);
            printf("  SHM_NAME            shared-memory segment (created by the command deck; if\n");
            printf("                        missing it is created EMPTY, for headless golden runs)\n");
            printf("  --screenshot FILE   capture the backbuffer of frame N as PPM and exit\n");
            printf("  --screenshot-frame N  frame to capture (default 30)\n");
            printf("  --seed N            fixed RNG seed (deterministic starfield; golden renders)\n");
            printf("  --no-rotate         disable the automatic camera orbit\n");
            printf("  --no-msaa           disable multisampling (forces 1 sample for deterministic golden renders)\n");
            printf("  --window WxH        window extent (default %dx%d)\n",
                   VIEW_WINDOW_WIDTH, VIEW_WINDOW_HEIGHT);
            printf("  --version           display version information and exit\n");
            printf("  --help, -h          display this help and exit\n");
            glfwTerminate();
            free(app);
            return 0;
        } else if (app->shm_fd == -1) {
            /* First non-option argument: SHM segment (legacy behavior).
             * If the segment does not exist yet, CREATE it (zeroed,
             * sizeof(SharedIPC)) instead of silently running stateless:
             * this is what the headless golden/screenshot runs need —
             * an empty-but-present scene makes the CPU-driven path take
             * its normal shm code path (starfield included) so both
             * render paths compare apples to apples. The command deck
             * always creates the segment before spawning the viewer, so
             * the normal flow is unchanged (O_EXCL never succeeds there). */
            bool shm_created = false;
            app->shm_fd = shm_open(argv[ai], O_RDWR, 0666);
            if (app->shm_fd == -1) {
                int fd_new = shm_open(argv[ai], O_RDWR | O_CREAT | O_EXCL, 0600);
                if (fd_new != -1) {
                    if (ftruncate(fd_new, sizeof(SharedIPC)) == 0) {
                        shm_unlink(argv[ai]); /* no orphan segments */
                        app->shm_fd = fd_new;
                        shm_created = true;
                    } else {
                        shm_unlink(argv[ai]);
                        close(fd_new);
                    }
                }
            }
            if (app->shm_fd != -1 && !shm_created) {
                /* A segment from the command deck: same layout or nothing
                 * (a shorter segment would SIGBUS on access). */
                struct stat shm_st;
                const char *why = (fstat(app->shm_fd, &shm_st) != 0) ? "cannot stat the shared memory segment"
                                                                     : shm_layout_check_size((long long)shm_st.st_size);
                if (why) {
                    fprintf(stderr, "[VULKAN] FATAL: %s.\n", why);
                    exit(1);
                }
            }
            if (app->shm_fd != -1) {
                app->shm = mmap(NULL, sizeof(SharedIPC), PROT_READ | PROT_WRITE, MAP_SHARED, app->shm_fd, 0);
                if (app->shm == MAP_FAILED) {
                    app->shm = NULL;
                } else if (shm_created) {
                    shm_layout_stamp(app->shm);
                } else {
                    const char *why = shm_layout_check(app->shm);
                    if (why) {
                        fprintf(stderr, "[VULKAN] FATAL: %s.\n", why);
                        exit(1);
                    }
                }
            }
        }
    }

    /* Deterministic RNG: seed BEFORE the starfields are generated
       (gdd_init / createStarfield both draw from rand()). */
    if (s_rng_seed >= 0) srand(s_rng_seed);

    app->window = glfwCreateWindow(win_w, win_h, "Space GL", NULL, NULL);
    if (!app->window) {
        free(app);
        return 1;
    }
    
    app->angleX = 0.0f;
    app->angleY = 0.0f;
    app->cameraDist = QG_CAMERA_DEFAULT;
    app->autoRotate = true;
    if (s_no_rotate) app->autoRotate = false; /* --no-rotate (golden renders) */
    app->bridgeAnim = 0.0f;
    app->showBridge = 0;

    /* Pilot mode init */
    app->pilotAnim      = 0.0f;
    app->showPilot      = 0;
    app->pilotHeading   = 0.0f;
    app->pilotMark      = 0.0f;
    app->pilotRoll      = 0.0f;
    app->pilotLastSpeed = 0.0f;
    app->jsLastImpTime  = 0.0;
    app->jsLastPosTime  = 0.0;
    app->jsLastPhaTime  = 0.0;
    app->jsLastTorTime  = 0.0;
    app->jsPrevHatX     = 0.0f;
    app->jsPrevHatY     = 0.0f;
    app->jsLastImpH     = -1.0; /* change-detector sentinels (2026.10.06.07) */
    app->jsLastImpM     = -1.0;
    app->jsLastImpS     = -1.0;
    app->jsLastThrS     = -1.0; /* throttle 1-arg channel (2026.10.07.02)   */
    app->jsLastThrTime  = 0.0;
    app->jsLastVecH     = -1.0; /* vector-thruster channel (2026.10.07.02)  */
    app->jsLastVecM     = -1.0;
    app->jsLastVecS     = -1.0;
    app->jsLastVecTime  = 0.0;
    app->jsLastPosH     = -1.0; /* pos dedup (2026.10.07.02)                */
    app->jsLastPosM     = -1.0;
    app->jsLastPosR     = -1.0;
    app->jsRetroActive  = 0;    /* retro latch (2026.10.07.01)               */
    memset(app->jsPrevBtns, 0, sizeof(app->jsPrevBtns));

    /* Detect T.16000M FCS (or any Thrustmaster joystick) */
    app->jsIndex = -1;
    for (int ji = GLFW_JOYSTICK_1; ji <= GLFW_JOYSTICK_LAST; ji++) {
        if (glfwJoystickPresent(ji)) {
            const char *jname = glfwGetJoystickName(ji);
            if (jname && (strstr(jname, "T.16000M") || strstr(jname, "Thrustmaster"))) {
                app->jsIndex = ji;
                printf("[PILOT] Flight stick detected: \"%s\" (GLFW joystick %d)\n",
                       jname, ji);
                break;
            }
        }
    }
    if (app->jsIndex < 0)
        printf("[PILOT] No Thrustmaster flight stick found — pilot mode input disabled.\n");

    
    glfwSetWindowUserPointer(app->window, app);
    glfwSetKeyCallback(app->window, key_callback);
    /* The window is resizable: without this callback a resize leaves
       the swapchain at its old extent, vkAcquireNextImageKHR returns
       VK_ERROR_OUT_OF_DATE_KHR on every frame and the display freezes
       (see recreateSwapChain). */
    glfwSetFramebufferSizeCallback(app->window, framebufferSizeCallback);
    
    initVulkan(app); 

    /* GPU-driven path: create the compute/scene pipelines and the
     * per-frame ring. On failure app->gdd stays NULL and the client
     * transparently runs the CPU-driven path. */
    if (app->gdd_wanted && !gdd_init(app, s_no_msaa ? GDD_MSAA_OFF : GDD_MSAA_4X)) {
        app->gdd_wanted = false;
        SG_WARNING(SG_CAT_VULKAN, "GPU-driven path unavailable: falling back to CPU-driven rendering");
        printf("[GDD] GPU-driven path unavailable: falling back to CPU-driven rendering\n");
        if (s_shot_path[0]) {
            fprintf(stderr, "[GDD] ERROR: screenshot requested with SPACEGL_GPD=1 but GPU-driven path failed to initialize\n");
            cleanup(app);
            free(app);
            return 1;
        }
    }
    if (app->gdd) {
        SG_SUCCESS(SG_CAT_VULKAN, "GPU-driven path ACTIVE (beams/effects render via gdd_build_frame)");
    } else {
        SG_SUCCESS(SG_CAT_VULKAN, "CPU-driven path ACTIVE (beams/effects render via recordCommandBuffer)");
    }
    
    if (app->shm) {
        pid_t tp = getppid();
        if (argc > 1) {
            char *p = strrchr(argv[1], '_');
            if (p) {
                tp = atoi(p+1);
            }
        }
        if (tp > 1) {
            kill(tp, SIGUSR2);
        }
    }
    
    mainLoop(app);
    cleanup(app);
    free(app);
    return 0;
}

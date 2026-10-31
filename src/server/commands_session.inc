/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * commands_session.inc — command handlers: captain session (zztop, xxx, cha).
 * Part of the command handlers, split by domain from commands_handlers.inc
 * (2026.10.31.15) without any code change; included, in this order, by
 * commands_handlers.inc into commands.c (one translation unit).
 */
void handle_zztop(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* permanent deletion of commander profile */
    send_server_msg(i, "COMPUTER", "TOTAL SYSTEM WIPE INITIATED. YOUR PROFILE IS BEING DELETED.");
    
    /* TRANSFER HULL TO DERELICTS */
    spawn_derelict(players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].faction, players[i].ship_class, players[i].name);
    push_server_event(i, IPC_EV_BOOM, players[i].state.s1, players[i].state.s2, players[i].state.s3, 0, 0, 0, 1);

    /* PERMANENT PROFILE DELETION (disk): remove the identity record and all
       frequency keys so the "purged" profile cannot simply log in again.
       The name was sanitized at login ([A-Za-z0-9_-], max 32 chars), so the
       constructed paths cannot escape the captains/ tree. */
    {
        char deleted_name[64];
        char rel_dir[160];
        char dir_path[1024];
        char file_path[1088];
        memcpy(deleted_name, players[i].name, 64);
        deleted_name[63] = '\0';
        if (deleted_name[0] != '\0') {
            snprintf(rel_dir, sizeof(rel_dir), "captains/%s", deleted_name);
            server_data_path(dir_path, sizeof(dir_path), rel_dir);
            snprintf(file_path, sizeof(file_path), "%s/identity.hash", dir_path);
            unlink(file_path);
            snprintf(file_path, sizeof(file_path), "%s/identity.salt", dir_path);
            unlink(file_path);
            for (int k = 1; k <= MAX_CRYPTO_ALGOS; k++) {
                snprintf(file_path, sizeof(file_path), "%s/algo_%d.key", dir_path, k);
                unlink(file_path);
                snprintf(file_path, sizeof(file_path), "%s/algo_%d_private.key", dir_path, k);
                unlink(file_path);
            }
            rmdir(dir_path);
            slog("\033[1;33m[PROFILE DELETION]\033[0m On-disk identity record for \033[1;37m%s\033[0m removed (%s).\n", deleted_name, dir_path);
        }
    }

    /* CLEAR DATA FROM MEMORY */
    memset(players[i].name, 0, 64);
    players[i].active = 0;
    
    /* Disconnect socket if exists */
    if (players[i].socket > 0) {
        /* Note: socket closure might be handled by the main loop when it detects active=0 or name empty */
    }

    /* Trigger permanent save to galaxy.dat */
    save_galaxy();
    
    send_server_msg(i, "SERVER", "COMMANDER PROFILE PURGED FROM GALAXY DATABASE. CONNECTION TERMINATED.");
    *should_disconnect = true;
}

void handle_xxx(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* Formal greeting to the captain */
    char greeting[256];
    snprintf(greeting, sizeof(greeting), "Saluti, Capitano %s. Protocollo di riposizionamento tattico d'emergenza inizializzato.", players[i].name);
    send_server_msg(i, "COMPUTER", greeting);

    /* TRANSFER HULL TO DERELICTS */
    spawn_derelict(players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].faction, players[i].ship_class, players[i].name);
    push_server_event(i, IPC_EV_BOOM, players[i].state.s1, players[i].state.s2, players[i].state.s3, 0, 0, 0, 1);

    /* EMERGENCY REENTRY PROTOCOL */
    rescue_player(i, &RESCUE_PARAMS_TACTICAL, NULL);
    players[i].death_timer = 0;

    send_server_msg(i, "COMPUTER", "Riposizionamento completato. Sistemi stabilizzati.");
    send_server_msg(i, "COMMAND", "EMERGENCY REENTRY: Vessell relocated to safe sector via tactical warp.");
}

void handle_cha(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[1] < THRESHOLD_SYS_CRITICAL || players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) { send_server_msg(i, "COMPUTER", "CHASE FAILURE."); return; }
    if (players[i].state.energy < (uint64_t)(COST_ACTION_HIGH + COST_ACTION_MED)) { send_server_msg(i, "COMPUTER", "Insufficient energy."); return; }
    int tid = players[i].state.lock_target;
    if (tid > 0) {
        players[i].state.energy -= (uint64_t)(COST_ACTION_HIGH + COST_ACTION_MED); 
        players[i].nav_state = NAV_STATE_CHASE;
        players[i].hyper_speed = 0; /* Reset speed to prevent initial overshoot */
        char msg[64];
        snprintf(msg, sizeof(msg), "Chase mode engaged for target ID %d.", tid);
        send_server_msg(i, "HELMSMAN", msg);
    } else send_server_msg(i, "COMPUTER", "No target locked.");
}


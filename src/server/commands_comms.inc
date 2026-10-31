/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * commands_comms.inc — command handlers: radio frequencies and encryption levels (enc, enc2, enc3, enc4).
 * Part of the command handlers, split by domain from commands_handlers.inc
 * (2026.10.31.15) without any code change; included, in this order, by
 * commands_handlers.inc into commands.c (one translation unit).
 */
void handle_enc_common(int i, const char *params, int level, bool *should_disconnect) {
    (void)should_disconnect;
    if (players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "COMPUTER", "CRYPTOGRAPHIC FAILURE: Logic core damaged. Encryption systems offline.");
        return;
    }

    int cost = COST_ENCRYPTION_SHIFT;
    bool is_pqc = strstr(params, "pqc") || strstr(params, "kyber") || strstr(params, "mceliece") || strstr(params, "dilithium");
    if (is_pqc) {
        cost = COST_PQC_INIT;
        if (players[i].state.system_health[6] < THRESHOLD_SYS_DEGRADED) {
            send_server_msg(i, "SCIENCE", "Quantum Tunnel initialization failed. Computer integrity must be above 50% for PQC.");
            return;
        }
    }

    if (players[i].state.energy < (uint64_t)cost) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for cryptographic frequency shift.");
        return;
    }

    bool valid = true;
    char target_name[64] = "";
    int found_idx = -1;

    if (level == 4) {
        if (sscanf(params, " %63s", target_name) == 1) {
            for(int j=0; j<MAX_CLIENTS; j++) {
                if(players[j].active && strcmp(players[j].name, target_name) == 0) {
                    found_idx = j;
                    break;
                }
            }
            if (found_idx != -1) {
                players[i].radio_lock_target = found_idx + 1;
                send_server_msg(i, "COMPUTER", "LEVEL D: EXCLUSIVE RADIO LINK LOCKED.");
            } else {
                send_server_msg(i, "COMPUTER", "ERROR: Target Captain not found for Exclusive Link.");
                valid = false;
            }
        } else {
            send_server_msg(i, "COMPUTER", "Usage: enc4 <CaptainName> <algo>");
            valid = false;
        }
    } else if (level == 3) {
        if (sscanf(params, " %63s", target_name) == 1) {
            /* enc3 uses target but doesn't hard-lock radio */
            players[i].radio_lock_target = 0;
        } else {
            send_server_msg(i, "COMPUTER", "Usage: enc3 <CaptainName> <algo>");
            valid = false;
        }
    } else {
        players[i].radio_lock_target = 0;
    }

    if (valid) {
        /* Clear previous level flags, keep signature 0x01 */
        players[i].state.encryption_flags &= 0x01; 

        if (strstr(params, "aes")) {
            players[i].state.shm_crypto_algo = CRYPTO_AES;
            players[i].state.encryption_flags |= 0x01;
            if (level == 4) { players[i].state.encryption_flags |= 0x08; send_server_msg(i, "COMPUTER", "EXCLUSIVE LINK ACTIVE: AES-256."); }
            else if (level == 3) { players[i].state.encryption_flags |= 0x04; send_server_msg(i, "COMPUTER", "PEER-TO-PEER ENCRYPTION ACTIVE: AES-256."); }
            else if (level == 2) { players[i].state.encryption_flags |= 0x02; send_server_msg(i, "COMPUTER", "IDENTITY ENCRYPTION ACTIVE: AES-256."); }
            else send_server_msg(i, "COMPUTER", "Deep Space encryption: AES-256-GCM ACTIVE.");
        } else if (strstr(params, "chacha")) {
            players[i].state.shm_crypto_algo = CRYPTO_CHACHA;
            players[i].state.encryption_flags |= 0x01;
            if (level == 4) { players[i].state.encryption_flags |= 0x08; send_server_msg(i, "COMPUTER", "EXCLUSIVE LINK ACTIVE: CHACHA20."); }
            else if (level == 3) { players[i].state.encryption_flags |= 0x04; send_server_msg(i, "COMPUTER", "PEER-TO-PEER ENCRYPTION ACTIVE: CHACHA20."); }
            else if (level == 2) { players[i].state.encryption_flags |= 0x02; send_server_msg(i, "COMPUTER", "IDENTITY ENCRYPTION ACTIVE: CHACHA20."); }
            else send_server_msg(i, "COMPUTER", "Deep Space encryption: CHACHA20-POLY1305 ACTIVE.");
        } else if (strstr(params, "aria")) {
            players[i].state.shm_crypto_algo = CRYPTO_ARIA;
            players[i].state.encryption_flags |= 0x01;
            if (level == 4) players[i].state.encryption_flags |= 0x08;
            else if (level == 3) players[i].state.encryption_flags |= 0x04;
            else if (level == 2) players[i].state.encryption_flags |= 0x02;
            send_server_msg(i, "COMPUTER", "Deep Space encryption: ARIA-256-GCM ACTIVE.");
        } else if (strstr(params, "pqc") || strstr(params, "kyber")) {
            players[i].state.shm_crypto_algo = CRYPTO_PQC;
            players[i].state.encryption_flags |= 0x01;
            if (level == 4) players[i].state.encryption_flags |= 0x08;
            else if (level == 3) players[i].state.encryption_flags |= 0x04;
            else if (level == 2) players[i].state.encryption_flags |= 0x02;
            send_server_msg(i, "COMPUTER", "Deep Space encryption: ML-KEM-1024 slot ACTIVE (experimental alias: AES-256-GCM).");
            send_server_msg(i, "SCIENCE", "Post-quantum slot is an experimental alias in this build; the underlying cipher is AES-256-GCM.");
        } else if (strstr(params, "off")) {
            players[i].state.shm_crypto_algo = CRYPTO_NONE;
            players[i].state.encryption_flags = 0;
            players[i].radio_lock_target = 0;
            send_server_msg(i, "COMPUTER", "WARNING: Encryption DISABLED. Signal is now RAW.");
            cost = 0;
        } else {
            /* Fallback generic algo detection for others */
            struct { const char* match; int algo; const char* name; } others[] = {
                {"camellia", CRYPTO_CAMELLIA, "CAMELLIA-256-CTR"}, {"seed", CRYPTO_SEED, "SEED-CBC"},
                {"cast", CRYPTO_CAST5, "CAST5-CBC"}, {"idea", CRYPTO_IDEA, "IDEA-CBC"},
                {"3des", CRYPTO_3DES, "DES-EDE3-CBC"}, {"bf", CRYPTO_BLOWFISH, "BLOWFISH-CBC"},
                {"blowfish", CRYPTO_BLOWFISH, "BLOWFISH-CBC"}, {"rc4", CRYPTO_RC4, "RC4-STREAM"},
                {"des", CRYPTO_DES, "DES-CBC"}, {"mceliece", CRYPTO_MCELIECE, "CLASSIC MCELIECE"},
                {"dilithium", CRYPTO_DILITHIUM, "DILITHIUM-ML-DSA"}, {"serpent", CRYPTO_SERPENT, "SERPENT-256-GCM"},
                {"twofish", CRYPTO_TWOFISH, "TWOFISH-256-CBC"}, {"sm4", CRYPTO_SM4, "SM4-CBC"},
                {"ascon", CRYPTO_ASCON, "ASCON-128"}, {"present", CRYPTO_PRESENT, "PRESENT-BIOMETRIC"},
                {"gost", CRYPTO_GOST, "GOST-KUZNYECHIK"}, {"salsa", CRYPTO_SALSA, "SALSA20"}, {NULL, 0, NULL}
            };
            bool found_other = false;
            for(int o=0; others[o].match; o++) {
                if (strstr(params, others[o].match)) {
                    players[i].state.shm_crypto_algo = others[o].algo;
                    players[i].state.encryption_flags |= 0x01;
                    if (level == 4) players[i].state.encryption_flags |= 0x08;
                    else if (level == 3) players[i].state.encryption_flags |= 0x04;
                    else if (level == 2) players[i].state.encryption_flags |= 0x02;
                    char msg[128]; sprintf(msg, "Deep Space encryption: %s ACTIVE.", others[o].name);
                    send_server_msg(i, "COMPUTER", msg);
                    found_other = true; break;
                }
            }
            if (!found_other) {
                send_server_msg(i, "COMPUTER", "Usage: enc[2|3|4] <params> <algo>");
                valid = false;
            }
        }
    }
    
    if (valid) {
        players[i].state.energy -= cost;
        
        /* Server-side logging of frequency shift */
        time_t now_enc = time(NULL);
        struct tm *t_enc = localtime(&now_enc);
        char time_enc[64];
        strftime(time_enc, sizeof(time_enc), "%Y-%m-%d %H:%M:%S", t_enc);
        
        const char *algo_name = "NONE";
        switch(players[i].state.shm_crypto_algo) {
            case CRYPTO_AES: algo_name = "AES"; break;
            case CRYPTO_CHACHA: algo_name = "CHACHA"; break;
            case CRYPTO_ARIA: algo_name = "ARIA"; break;
            case CRYPTO_CAMELLIA: algo_name = "CAMELLIA"; break;
            case CRYPTO_SEED: algo_name = "SEED"; break;
            case CRYPTO_CAST5: algo_name = "CAST"; break;
            case CRYPTO_IDEA: algo_name = "IDEA"; break;
            case CRYPTO_3DES: algo_name = "3DES"; break;
            case CRYPTO_BLOWFISH: algo_name = "BLOWFISH"; break;
            case CRYPTO_RC4: algo_name = "RC4"; break;
            case CRYPTO_DES: algo_name = "DES"; break;
            case CRYPTO_PQC: algo_name = "PQC"; break;
            case CRYPTO_MCELIECE: algo_name = "MCELIECE"; break;
            case CRYPTO_DILITHIUM: algo_name = "DILITHIUM"; break;
            case CRYPTO_SERPENT: algo_name = "SERPENT"; break;
            case CRYPTO_TWOFISH: algo_name = "TWOFISH"; break;
            case CRYPTO_SM4: algo_name = "SM4"; break;
            case CRYPTO_ASCON: algo_name = "ASCON"; break;
            case CRYPTO_PRESENT: algo_name = "PRESENT"; break;
            case CRYPTO_GOST: algo_name = "GOST"; break;
            case CRYPTO_SALSA: algo_name = "SALSA"; break;
        }
        slog("\033[1;34m[ENCRYPTION]\033[0m Captain \033[1;37m%-15s\033[0m tuned frequency to \033[1;36m%-8s\033[0m [\033[1;33m%s\033[0m]\n", 
               players[i].name, algo_name, time_enc);
    }
}

void handle_enc(int i, const char *params, bool *sd) { handle_enc_common(i, params, 1, sd); }
void handle_enc2(int i, const char *params, bool *sd) { handle_enc_common(i, params, 2, sd); }
void handle_enc3(int i, const char *params, bool *sd) { handle_enc_common(i, params, 3, sd); }
void handle_enc4(int i, const char *params, bool *sd) { handle_enc_common(i, params, 4, sd); }


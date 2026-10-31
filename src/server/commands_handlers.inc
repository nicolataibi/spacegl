/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * commands_handlers.inc — the command handlers, one file per domain
 * (2026.10.31.15: split from a single 4290-line file without any code
 * change). The order is the original one: later files use helpers that
 * earlier ones define (get_sensor_error, get_ship_class_name, ...), and the
 * preprocessed commands.c is byte-identical to the single-file version.
 */
#include "commands_comms.inc"
#include "commands_helm.inc"
#include "commands_session.inc"
#include "commands_sensors.inc"
#include "commands_tactical.inc"
#include "commands_operations.inc"
#include "commands_reports.inc"
#include "commands_special.inc"

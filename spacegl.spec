# Copyright (C) 2026 Nicola Taibi
%global rel 9
Name:           spacegl
Version:        2026.02.09
Release:        %{rel}%{?dist}
Summary:        Space GL: A space exploration & combat game, Multi-User Client-Server Edition
License:        GPL-3.0-or-later
URL:            https://github.com/nicolataibi/spacegl
Source0:        https://github.com/nicolataibi/spacegl/archive/refs/tags/%{version}-%{rel}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  freeglut-devel
BuildRequires:  mesa-libGLU-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  glew-devel
BuildRequires:  openssl-devel
BuildRequires:  desktop-file-utils

Requires:       freeglut
Requires:       mesa-libGLU
Requires:       mesa-libGL
Requires:       glew
Requires:       openssl
Requires:       %{name}-data = %{version}-%{release}

%description
Space GL is a high-performance 3D multi-user client-server game engine.
It features real-time galaxy synchronization via shared memory (SHM),
advanced cryptographic communication frequencies (AES, PQC, etc.),
and a technical 3D visualizer based on OpenGL and FreeGLUT.

%package data
Summary: Data files for %{name}
BuildArch: noarch
# 2. Aggiungi questa riga mancante:
Requires: %{name} = %{version}-%{release}

%description data
Data files (graphics, sounds, and images) for Space GL.

%prep
%setup -q -n %{name}-%{version}-%{rel}

%build
# Forza il ricalcolo dei flag di Fedora
%set_build_flags
# Compila forzando il rifacimento (evita il "Nothing to be done")
%make_build clean
%make_build

%check
# Ora il check passerà perché abbiamo sistemato il Makefile
%make_build check

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/%{name}
mkdir -p %{buildroot}%{_datadir}/%{name}/readme_assets
cp -p readme_assets/*.jpg %{buildroot}%{_datadir}/%{name}/readme_assets/
cp -p readme_assets/*.png %{buildroot}%{_datadir}/%{name}/readme_assets/

# Install binaries
install -p -m 0755 spacegl_server %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_client %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_3dview %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_viewer %{buildroot}%{_bindir}/

# Install helper scripts as user commands
install -p -m 0755 run_server.sh %{buildroot}%{_bindir}/%{name}-server
install -p -m 0755 run_client.sh %{buildroot}%{_bindir}/%{name}-client

# Create and install desktop file
mkdir -p %{buildroot}%{_datadir}/applications
cat > %{buildroot}%{_datadir}/applications/%{name}.desktop <<EOF
[Desktop Entry]
Name=Space GL
Comment=Space exploration & combat game
Exec=spacegl-client
Icon=applications-games
Terminal=true
Type=Application
Categories=Game;Simulation;
EOF

desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop

%files
%license LICENSE.txt
%doc README_it.md README.md HOWTO.txt

%{_bindir}/spacegl_server
%{_bindir}/spacegl_client
%{_bindir}/spacegl_3dview
%{_bindir}/spacegl_viewer
%{_bindir}/%{name}-server
%{_bindir}/%{name}-client
%{_datadir}/applications/%{name}.desktop

%files data
%dir %{_datadir}/%{name}/
%{_datadir}/%{name}/readme_assets/

%changelog
* Tue Feb 13 2026 Nicola Taibi <nicola.taibi.1967@gmail.com> - 2026.02.09-%{rel}
- NAV: *
From the code analysis, I found that the implementation of the nav command is structurally correct but lacks some critical checks for realism and game balancing:

   1. Lack of energy consumption: Currently, Hyperdrive travel does not consume energy, unlike the impulse engine (imp) which consumes 1 unit per tick.
   2. Lack of integrity check: The health status of the Hyperdrive system (index 0 of devices) is not verified before starting navigation.
   3. Absence of activation costs: A minimum energy level is not required to start the jump.
   4. Movement management: The calculation of the vector and galactic destination is correct, as is the state transition (ALIGN -> HYPERDRIVE -> REALIGN).

  Do you want me to proceed with the implementation of energy consumption and system integrity checks for the nav command?
......................
IMP: *
I analyzed the current implementation of the imp command and the movement logic in src/server/logic.c. Here too, as with nav, system integrity checks and more consistent energy consumption management based on power allocation are missing.

  Here is what I will implement:
   1. In `handle_imp` (commands.c):
       * Impulse system integrity check (index 1): must be at least 10% to activate.
       * Activation cost: 100 energy units.
       * In case of reduced integrity (< 40%), there is a probability of malfunction during alignment.
   2. In `update_game_logic` (logic.c):
       * Tick-per-tick energy consumption based on impulse speed.
       * If Impulse system integrity drops to zero during flight, the engines shut down.
       * If energy runs out, the engines shut down.
.......................
POS: *
I examined the implementation of the pos command. Currently, the command orients the ship toward a specified Heading and Mark without consuming energy and without system integrity requirements.

  For consistency with nav and imp, I propose the following improvements:
   1. Integrity requirements: The positioning system (Auxiliary - index 9 or Impulse - index 1?) should be functional. Considering that pos uses maneuvering thrusters, it makes sense to link it to the Impulse system (index 1) or Auxiliary (index 9). I decide on Impulse since the physical orientation of the ship usually depends on the maneuvering engines.
   2. Energy consumption: A minimum activation cost (e.g., 20 units) to reflect thruster usage.
   3. Minimum integrity: At least 10% health of the Impulse system.

  I am proceeding with the implementation of these checks.
........................
JUM: *
The implementation of the jum command (Wormhole Jump) is already quite solid, with checks on costs (5000 energy and 1 Aetherium) and coordinates. However, system integrity checks and risk management related to the ship's state are missing.

  Here are the improvements I intend to implement:
   1. Integrity requirements: The Wormhole jump is a complex maneuver. It must require that the Hyperdrive (index 0) and Sensors (index 2) systems be operational (at least 50% health each). If sensors are damaged, there is a risk of ending up in an unexpected point.
   2. Malfunction risk: If Hyperdrive integrity is between 50% and 75%, there is a probability that the jump fails or consumes resources without activating.
   3. Stress damage: The structural stress of an artificial Wormhole should cause slight random damage to the hull (1-3%).
.........................
SRS: *
The implementation of the srs command (Short Range Sensors) already includes error logic based on sensor health (via get_sensor_error) and conditional object display. However, some elements are missing to make it fully consistent with the other enhanced commands:

   1. Energy Cost: Currently, scanning is free. I propose a minimum cost (e.g., 10 energy units) to reflect active sensor use.
   2. Minimum Integrity Check: If the sensor system is completely destroyed (< 5%), the command should return nothing but a hardware error.
   3. Improved feedback: Add a message if sensors are disturbed by a nebula or an ion storm.
.........................
LRS: *
The implementation of the lrs command (Long Range Sensors) is already very detailed and handles telemetry errors based on sensor health well. However, for consistency with the srs command, I propose the following improvements:

   1. Energy Cost: Add a cost of 25 energy units for each long-range scan (more expensive than a short-range scan).
   2. Critical Integrity Check: If sensor integrity is below 15%, the long-range scan should fail completely (requires more precision than local scanning).
   3. Improved Feedback: If integrity is low (< 50%), show a specific warning at the beginning of the report.
..........................
PHA: *
The implementation of the pha command (Ion Beams) is already advanced, including:
   - Energy consumption and capacitor charging.
   - Damage mapping on directional shields (6 quadrants).
   - Damage to the hull and internal systems if shields are down.
   - Multipliers based on weapon power (pow) and hardware integrity.
   - Management of 'Renegade' status in case of friendly fire.

  However, I propose the following improvements to increase realism:
   1. Minimum Integrity Check: The Ion Beam system (ID 4) must have at least 10% health to function.
   2. Probability of Miss: If sensors (ID 2) are damaged (< 50%), I would add a probability of missing the target proportional to the damage.
   3. Overheating: If firing with maximum energy (e > 10000), there is a small probability (e.g., 5%) of slightly damaging the Ion Beam banks themselves.
............................
TOR: *
The implementation of the tor command (Plasma Torpedoes) already includes the following checks:
   - Integrity check of launch tubes (index 5, must be > 50%).
   - Check if tubes are already in launch or reload phase.
   - Launch block if the ship is cloaked.
   - Torpedo deduction from inventory.
   - Support for manual (Heading/Mark) or auto-guided (if there is a lock) launch.

  I propose the following improvements for consistency and realism:
   1. Launch Energy Cost: Each launch should consume a small amount of energy (e.g., 250 units) to power the ejection and guidance systems.
   2. Malfunction Risk (Misfire): If torpedo system integrity is between 50% and 75%, there is a probability that the launch fails but still consumes the torpedo.
   3. Guidance Error Probability: If sensors (ID 2) are damaged, automatic torpedo guidance should be less precise (this should be implemented in logic.c).
............................
SHE: *
The implementation of the she command (Shield Configuration) is very simple: it distributes the provided energy values into the 6 shield quadrants without any checks.

  I propose the following improvements to make it more realistic and balanced:
   1. Integrity Check: The shield system (ID 8) must be at least 10% to allow reconfiguration.
   2. Energy Cost: Each shield reconfiguration operation should cost a minimum of energy (e.g., 50 units).
   3. Charge Limit: Provided values cannot exceed maximum capacity (e.g., 10,000 units per quadrant).
   4. Reactor Consumption: Energy assigned to shields must be subtracted from the main reactor. Currently, the command seems to 'create' energy in shields out of nothing.
.............................
LOCK: *
The implementation of the lock command is extremely basic: it sets the target ID without any range, visibility, or targeting system integrity checks.

  I propose the following improvements:
   1. Integrity Check: The targeting system (Sensors - ID 2) must be functional (min 10%).
   2. Visibility/Range Check: You should not be able to lock onto a target if it is not detected by short-range sensors (same quadrant).
   3. Energy Cost: A minimum cost (e.g., 5 units) to power the active tracking system.
   4. Cloak Check: You should not be able to lock onto a cloaked target (unless it is from the same faction).
..............................
ENC: *
The current implementation of the enc command (Encryption) allows changing the tactical encryption algorithm. Currently, it is a free operation without hardware constraints.

  I propose the following improvements to make it consistent with the standard of other commands:
   1. Integrity Check: The encryption system is part of the Computer (ID 6). It must be functional (min 10%).
   2. Energy Cost: Each cryptographic frequency change should cost a minimum of energy (e.g., 50 units) to reflect key recalculation and tunnel resynchronization.
   3. PQC Algorithm (Post-Quantum): Being the most complex algorithm, it should cost more (e.g., 250 units) and require higher computer integrity (min 50%).
..............................
SCAN: *
The implementation of the scan command is very rich in textual information for each object type. However, as with other commands, it has room for improvement regarding costs and hardware requirements:

   1. Integrity Check: Detailed scanning requires a highly functional sensor system (ID 2). I would suggest a minimum of 20% health to get a readable report.
   2. Energy Cost: Detailed scanning of a specific object should cost more than a generic SRS scan (e.g., 20 energy units).
   3. Range Check: Currently, the command only works in the current quadrant (already implemented via q1, q2, q3 checks), but there is no sector distance check for very small or cloaked objects.
   4. Quality Feedback: If sensor integrity is between 20% and 50%, some data (e.g., exact percentages) should appear as 'Scrambled' or with noise.
..............................
POW: *
The implementation of the pow command (Power Allocation) is correct in ratio calculation, but as with other commands, it lacks hardware constraints and operational costs:

   1. Integrity Check: Power distribution is managed by the Computer (ID 6) or the Engineering system (Auxiliary - ID 9). I suggest linking it to the Computer (min 10% health).
   2. Energy Cost: Reconfiguring reactor energy flows should cost a minimum of energy (e.g., 50 units).
   3. Response Times: If the computer is damaged (< 50%), there could be a delay in applying the new configuration (although in tick-based this is complex, we could add an error probability or doubled energy cost).
...............................
PSY: *
The implementation of the psy command (Psychological Warfare / Corbomite Bluff) is interesting but can be made deeper and more coherent:

   1. Hardware Requirements: The bluff requires a powerful and credible radio transmission. I suggest linking it to the integrity of the Auxiliary (ID 9) or Computer (ID 6) system. I decide on Computer (min 20%).
   2. Energy Cost: Generating fake atomic/antimatter threat signals on all frequencies should cost energy (e.g., 500 units).
   3. Retaliation Risk: If the bluff fails, enemies should become more aggressive (AI_STATE_CHASE) instead of just ignoring the message.
   4. Resource Check: Currently consumes anti_matter_count. This is fine, but I would add feedback if the inventory is empty.
.................................
AUX: *
The implementation of the aux command (Auxiliary Systems) handles several sub-functions: jettison, probe, report, recover.

  I propose the following improvements for consistency with other commands:

   1. Reference Subsystem: Link aux operations to the Auxiliary system (ID 9). It must have a minimum integrity (e.g., 10%) to function.
   2. Energy costs:
       * probe: Currently free. It should cost energy for launch (e.g., 1000 units).
       * report: It should cost a small amount for long-range data reception (e.g., 50 units).
       * recover: Already provides a 500 energy bonus (probe core recovery), which is fine.
       * jettison: Emergency operation, it should be possible even with damaged systems, but requires minimum residual energy for ejection (e.g., 100 units).
   3. Integrity for Probe: Launching a probe requires precise targeting (Sensors - ID 2) and calculation (Computer - ID 6). I suggest min 25% for both.
..................................
DIS: *
The implementation of the dis command (Dismantle) correctly handles resource recovery from NPC wrecks and static derelicts.

  However, I propose the following improvements for consistency:
   1. Reference Subsystem: The dismantling operation requires tractor beams and engineering teams. It should be linked to the integrity of the Transporters (ID 3) or Auxiliary (ID 9) system. I decide on Transporters (min 15% health).
   2. Energy Cost: Dismantling a ship is a heavy operation. It should cost energy (e.g., 500 units).
   3. Range Check: Currently, the range is 1.5. I would keep it but add an energy check before execution.
   4. Variable Yield: Resource yield could slightly depend on the health of the Transporter system (ID 3).
...................................
BOR: *
The implementation of the bor command (Boarding Party) is already quite complete, handling various scenarios (player, NPC, wreck, platform) and an interactive menu.

  However, I propose the following improvements for consistency with other commands:
   1. Reference Subsystem: The operation requires heavy use of Transporters (ID 3). It must be functional (min 20%).
   2. Energy Cost: Currently costs 5000 units. This is appropriate, but the check should be done after verifying system integrity.
   3. Success Probability: The probability of success should scale with Transporter system integrity and target shield status (if applicable). It is currently fixed at 45%.
   4. Improved Feedback: Add a message if transporters are damaged.
   5. after a choice in the wreck menu, it should be marked as inactive (destroyed/emptied), preventing multiple boardings.
   6. after my choice from the menu and at the end of the relative boarding procedure, the wreck must explode as in the dismantle command, assigning the due resources.  
................................
MIN: *
The implementation of the min command (Planetary/Asteroid Mining) correctly handles resource extraction from nearby planets and asteroids.

  I propose the following improvements to make it consistent with other commands:
   1. Integrity Check: Mining requires the use of extractor beams. It should be linked to the integrity of the Transporters (ID 3) or Auxiliary (ID 9) system. I decide on Transporters (min 15% health).
   2. Energy Cost: Each extraction operation should cost energy (e.g., 250 units).
   3. Variable Yield: The amount extracted should slightly depend on the health of the Transporter system (ID 3).
   4. Improved Feedback: Provide details on the quantity and type of resource actually extracted.
...............................
DOC: *
The implementation of the doc command (Docking) is already very solid: it handles the full restoration of energy, torpedoes, hulls, systems, and personnel, plus the debriefing of prisoners.

  I propose the following improvements to make it more realistic:
   1. Docking Time: The operation should not be instantaneous. I suggest introducing a docking timer (e.g., 10 seconds) during which the ship is locked and vulnerable (shields disabled or powered by the station).
   2. Faction Check: Currently, one can dock at any base. It should only be possible at bases of one's own faction or neutral bases (if any exist).
   3. Operational Cost: Although replenishment is 'included', docking requires maneuvering precision (Impulse - ID 1) and docking systems (Auxiliary - ID 9).
   4. 3D viewer HUD: insert SHIP STATUS: DOCKED / UNDOCKED.

  However, seeing the current code, instantaneous docking seems to be a design choice for simplicity. I will implement at least the faction check and a minimum integrity requirement for maneuvering systems.
.........................................
CON: *
The implementation of the con command (Resource Converter) handles the conversion of raw materials into energy or torpedoes stored in the cargo bay.

  I propose the following improvements:
   1. Integrity Check: Resource conversion is a complex industrial operation. It should be linked to the integrity of the Auxiliary system (ID 9). I would suggest a minimum of 15% health.
   2. Energy Cost: The conversion process itself requires energy from the main reactor (e.g., 100 units for each conversion cycle).
   3. Efficiency: Conversion yield could depend on the health of the Auxiliary system (ID 9).
   4. Improved Feedback: Provide precise details on what was converted and how much was obtained.
...................................
LOAD: *
The implementation of the load command (Load Systems) allows transferring energy or torpedoes from the cargo bay to active systems.

  I propose the following improvements:
   1. Integrity Check: Loading active systems from the cargo bay requires the use of power lines and internal transfer systems. It should be linked to the integrity of the Auxiliary system (ID 9). I would suggest a minimum of 10% health.
   2. Energy Cost: Even though energy is being transferred, the pumping and stabilization process should cost a small amount of energy from the main reactor (e.g., 25 units) to avoid abuse of continuous small transfers.
   3. Improved Feedback: Provide precise details on the amount actually loaded.
..........................
HULL: *
The implementation of the hull command (Hull Reinforcement) is functional but can be improved for consistency:

   1. Integrity Check: Hull reinforcement requires welding and material integration systems. It should be linked to the integrity of the Auxiliary (ID 9) or Transporters (ID 3). I decide on Auxiliary (min 10% health).
   2. Energy Cost: Integrating composite material into the hull should cost energy (e.g., 1000 units) to power the industrial beams.
   3. Plating Limit: Add a maximum limit to composite plating (e.g., 5000 units) to avoid 'immortal' ships accumulating resources indefinitely.
   4. Improved Feedback: Provide the current plating value after reinforcement.
.............................
REP: *
The implementation of the rep command (Repair) correctly handles system integrity restoration using resources (Neo-Titanium and Synaptics).

  I propose the following improvements to make it more realistic and balanced:
   1. Support Integrity Check: Repairs are coordinated by the Computer (ID 6) and the Auxiliary (ID 9) system. I suggest that at least one of the two be functional (min 10%) to be able to start repairs on other systems.
   2. Energy Cost: Repairing a complex system requires energy from the main reactor (e.g., 500 units).
   3. Partial Repair: If resources are scarce, we could allow partial repair (e.g., spending half to reach 50%), but for now, I would keep restoration at 100% for simplicity, while adding the energy cost.
   4. Improved Feedback: Provide the name of the repaired system in the confirmation message.
.............................
FIX: *
The implementation of the fix command (Field Hull Repair) correctly handles partial hull integrity restoration using resources (Graphene and Neo-Titanium).

  I propose the following improvements for consistency:
   1. Support Integrity Check: Field repairs require welding and material management systems. They should be linked to the integrity of the Auxiliary system (ID 9). I would suggest a minimum of 10% health.
   2. Energy Cost: The structural repair operation requires energy (e.g., 500 units).
   3. Efficiency: The amount repaired could slightly depend on the health of the Auxiliary system (ID 9). It is currently fixed at 15%.
   4. Improved Feedback: Provide the current hull status in the confirmation message (already present, but improvable).
..............................
INV: *
The implementation of the inv command (Inventory Report) is simple and provides a list of resources in the cargo bay.

  I propose the following improvements for consistency:
   1. Integrity Check: Access to the inventory is managed by the Engineering/Logistics (Auxiliary - ID 9) or Computer (ID 6) system. I suggest linking it to the Computer (min 5% health) as it is a database consultation.
   2. Energy Cost: Scanning cargo manifests should cost a minimum of energy (e.g., 5 units).
   3. Quality Feedback: If the computer is damaged (< 30%), some data might be imprecise or show '??'.
...........................
WHO: *
The implementation of the who command (Captains Registry) provides a list of active captains.

  I propose the following improvements to make it consistent:
   1. Integrity Check: Access to the galactic registry requires the Communication/Sensor (Auxiliary - ID 9 or Computer - ID 6) system. I decide on Computer (ID 6) (min 5% health).
   2. Energy Cost: Synchronization with the central Alliance Command database should cost a minimum of energy (e.g., 10 units).
   3. Range/Visibility: If the computer is damaged (< 40%), captains in very distant quadrants might not be displayed or appear as '??'.
   4. Improved Feedback: Include affiliation (Faction) if possible.
...................................
CAL: *
The implementation of the cal command (Navigational Computer) provides precise calculations for Hyperdrive routes.

  I propose the following improvements for consistency:
   1. Integrity Check: Calculating hyperspace routes requires an advanced calculation system. It should be linked to the integrity of the Computer (ID 6) system. I suggest a minimum of 10% health.
   2. Energy Cost: Each complex calculation should cost a small amount of energy (e.g., 25 units).
   3. Data Scrambling: If the computer is damaged (< 50%), calculation results might be imprecise or show '??'.
...........................
ICAL: *
The implementation of the ical command (Impulse Calculator) provides precision calculations for sub-light navigation within the quadrant.

  I propose the following improvements for consistency with other calculation systems:
   1. Integrity Check: This command too must depend on the Computer (ID 6) system, with a minimum of 10% health to function.
   2. Energy Cost: Each vector calculation should cost a small amount of energy (e.g., 10 units).
   3. Data Scrambling: If computer integrity is below 50%, the results (H, M, ETA) might appear corrupted or show '??'.
............................
APR: *
The implementation of the apr command (Approach) is very extensive and correctly handles the approach to almost all galactic entities.

  I propose the following improvements for consistency with other navigation commands:
   1. Integrity Check: The approach autopilot depends on both the Computer (ID 6) and Impulse (ID 1) systems. Both should be functional (min 10% health).
   2. Energy Cost: Activating the autopilot should cost a minimum of energy (e.g., 100 units) for trajectory calculation and engine startup.
   3. Invisibility (Cloak): You should not be able to target a cloaked target (unless it belongs to your own faction).
   4. Improved Feedback: Provide the name of the entity being approached.
....................................
CHA: *
The implementation of the cha command (Chase) is extremely simple: it activates chase mode if there is a locked target.

  I propose the following improvements for consistency:
   1. Integrity Check: Active chasing requires both the Impulse (ID 1) system for movement and the Computer (ID 6) for continuous calculation of the intercept vector. I suggest min 10% for both.
   2. Energy Cost: Activating tactical chase mode should cost energy (e.g., 150 units) to reflect the intensity of calculation and maneuvering.
   3. Target Check: You cannot chase a target that is not currently locked. This is already present, but I would add an energy check.
   4. Improved Feedback: Include the name or ID of the chased target.
.......................................
SCO: *
The implementation of the sco command (Solar Scooping) correctly handles energy collection from stars, including shield damage from extreme heat.

  I propose the following improvements for consistency:
   1. Integrity Check: Solar collection requires energy collectors and containment systems. It should be linked to the integrity of the Auxiliary (ID 9) system. I would suggest a minimum of 15% health.
   2. Operational Energy Cost: Activating collector beams and the cooling system should cost a small amount of energy (e.g., 100 units).
   3. Collection Logic: Check proximity to a star in the current quadrant.
   4. Variable Yield: The amount of energy collected could slightly depend on the health of the Auxiliary (ID 9) system.
   5. Structural Damage: If shields are at zero, the heat should directly damage the hull integrity and internal systems (Life Support).
   6. Detailed Feedback: Message with the amount of energy stored in the cargo bay.
........................................
HAR: *
The implementation of the har command (Antimatter Harvest) handles antimatter collection from black holes, including a strong impact on shields from radiation and gravity.

  I propose the following improvements for consistency:
   1. Integrity Check: Antimatter collection is an extremely risky and complex operation. It should be linked to the integrity of the Auxiliary (ID 9) system. I suggest a minimum of 25% health (higher than sco because of the danger of black holes).
   2. Operational Energy Cost: Activating magnetic containment fields should cost energy (e.g., 500 units).
   3. Collection Logic: Check proximity to a Black Hole in the current quadrant (limited range).
   4. Variable Yield: The amount of antimatter and Aetherium crystals collected could depend on the health of the Auxiliary (ID 9) system.
   5. Structural and System Damage: Massive drainage of a random shield. If the shield doesn't hold, I apply heavy damage to the hull and critical systems: Sensors (2), Computer (6), and Life Support (7).
   6. Detailed Feedback: Warning message for gravitational tides and booty summary.
...........................................
STA: *
The implementation of the sta command (Status Report) is very complete and provides a diagnostic overview of the entire ship.

  I propose the following improvements for consistency:
   1. Integrity Check: Generating a full diagnostic report requires a functional Computer (ID 6) system. I suggest a minimum of 5% health.
   2. Energy Cost: Each full diagnostic scan should cost a minimum of energy (e.g., 10 units).
   3. Data Scrambling: If computer integrity is below 30%, telemetry and status data should appear obscured or with errors.
   4. Inclusion of Missing Data: Add the integrity of Shields (8) and Auxiliary (9) systems which currently seem truncated or missing in the print cycle (the code reads up to s<8).
......................................
DAM: *
The implementation of the dam command (Damage Report) provides a summary of hull integrity and the first 8 systems.

  I propose the following improvements for consistency:
   1. Integrity Check: Damage diagnostics requires the Computer (ID 6) or the Auxiliary (ID 9) system. I decide on Computer (min 5% health).
   2. Energy Cost: A scan of structural damage should cost a minimum of energy (e.g., 5 units).
   3. Inclusion of Missing Data: Complete the list by including all 10 systems (up to ID 9).
   4. Quality Feedback: If the computer is damaged (< 30%), data might be imprecise or show '??'.
.........................................
RAD: *
The management of the rad command (which generates a PKT_MESSAGE) is delegated to the broadcast_message function in src/server/net.c.

  I propose the following improvements for the messaging system:
   1. Energy Cost: Sending a message through subspace should have a minimum cost (e.g., 5 units) to avoid spam.
   2. Integrity Check: Communications depend on the Auxiliary (ID 9) or Computer (ID 6) system. I decide on Auxiliary (min 5% health) as the primary communication system.
   3. Encryption Management: The message is already handled with encrypt_payload in net.c, which applies the chosen algorithm. The broadcast logic should check if the sender has enough energy and integrity before forwarding.

Strategy:
   1. Find the sender.
   2. Verify Auxiliary integrity (min 5%) and Energy (min 5).
   3. If it fails, send a send_server_msg to the sender and abort the broadcast.
   4. If successful, subtract energy and proceed.
..............................
CLO: *
I am enhancing the clo command: I add an integrity check on the Auxiliary system (ID 9, min 15%) and an energy cost of 500 units for activation. I also implement a probability of failure if integrity is reduced.
I update the cloak logic in src/server/logic.c: if the Auxiliary system is destroyed (health < 5%) while the ship is cloaked, the cloak automatically deactivates with a warning message.
Verification of the clo command complete. Now the system requires an energy cost of 500 units for activation, a minimum health of the Auxiliary system of 15%, and provides for forced shutdown if integrity falls below 5%.
..................................
AXS: *
Verification of the axs visual command complete. It is now managed server-side with energy costs and integrity requirements.
.....................................
GRD: *
1. Verify Computer integrity (ID 6): requires at least 10% health.
   2. Subtract energy: 10 units for each activation/deactivation.
   3. Synchronize state: the flag is sent from the server to the client via PacketUpdate and applied to the shared memory for the 3D viewer. It is now managed server-side with energy costs and integrity requirements ('server-authoritative').
   4. Emergency deactivation: if computer integrity falls below 5%, the grid automatically disappears.
......................................
MAP: *
Verification of the map command complete. It is now managed server-side with energy costs and computer integrity requirements.
......................................
BRIDGE: *
The implementation of the bridge command is already complete and consistent with the established standards:
   1. Integrity Check: Requires the Computer system (ID 6) at 10%.
   2. Energy Cost: 10 units for each view change.
   3. Parameter Management: Supports all directions (top, bottom, left, right, up, down, rear, off) and intelligent switching based on the current position (above or below the hull).
   4. It is now managed server-side with energy costs and integrity requirements.
   5. Synchronization: Already integrated into logic.c and documented in the READMEs.
........................................
UND:
Implementation of the UND command (Undock). 
UNDOCK Command Summary:
   * Code: und or undock.
   * Requirements: Auxiliary System (ID 9) >= 10%.
   * Cost: 50 energy units.
   * Effect: Releases docking clamps and allows tactical and movement systems usage again.
.......................................
New wormhole sequence.

# Space GL: 3D Multi-User Client-Server Edition
## A space exploration & combat game
## "Per Tenebras, Lumen" (Through darkness, light)
### Website: https://github.com/nicolataibi/spacegl
### Authors: Nicola Taibi, Supported by Google Gemini
### Copyright (C) 2026 Nicola Taibi - Licensed under GPL-3.0-or-later
**Persistent Galaxy Tactical Navigation & Combat Simulator**

<table>
  <tr>
    <td><img src="readme_assets/StellarAlliance-LegacyClass.jpg" alt="Stellar Alliance Legacy Class" width="400"/></td>
    <td><img src="readme_assets/startup.jpg" alt="Space GL Startup" width="400"/></td>
  </tr>
  <tr>
    <td><img src="readme_assets/running.jpg" alt="Space GL Running" width="400"/></td>
    <td><img src="readme_assets/combat.jpg" alt="Space Combat" width="400"/></td>
  </tr>
  <tr>
    <td><img src="readme_assets/comets.jpg" alt="Comets and Asteroids" width="400"/></td>
    <td><img src="readme_assets/space-monster.jpg" alt="Space Monster" width="400"/></td>
  </tr>
  <tr>
    <td><img src="readme_assets/rift.jpg" alt="Spatial Rift" width="400"/></td>
    <td><img src="readme_assets/shields.jpg" alt="Defensive Shields" width="400"/></td>
  </tr>
  <tr>
    <td><img src="readme_assets/wormhole-enter.jpg" alt="Wormhole Entry" width="400"/></td>
    <td><img src="readme_assets/wormhole-exit.jpg" alt="Wormhole Exit" width="400"/></td>
  </tr>
</table>

Space GL is an advanced space simulator combining the strategic depth of classic 70s text-based games with a modern Client-Server architecture and hardware-accelerated 3D visualization.

---

## 🚀 Quick Start Guide

### 1. Install Dependencies (Linux)
```bash
# Ubuntu / Debian
sudo apt-get install build-essential freeglut3-dev libglu1-mesa-dev libglew-dev libssl-dev

# Fedora / Red Hat
sudo dnf groupinstall "Development Tools"
sudo dnf install freeglut-devel mesa-libGLU-devel glew-devel openssl-devel
```

### 2. Build
Compile the project to generate the updated executables:
```bash
make
```

### 3. Start Server
Launch the secure startup script. You will be asked to set a **Master Key** (secret password for the server):
```bash
./run_server.sh
```

### 4. Start Client
In another terminal, launch the client:
```bash
./run_client.sh
```
**Login Flow:**
1.  **Server IP:** Enter the server address.
2.  **Handshake:** The client validates the Master Key. If incorrect, it exits immediately for security.
3.  **Identification:** Only if the link is secured, you will be asked for your **Commander Name**.
4.  **Configuration:** If it's your first time, you will select your Faction and Ship Class.

---

## ⚙️ Configuration & Modding

Gameplay is entirely customizable via the centralized configuration file **`include/game_config.h`**.
By editing this file and recompiling with `make`, you can alter the physics and combat rules of your server.

Configurable parameters include:
*   **Resource Limits:** `MAX_ENERGY_CAPACITY`, `MAX_TORPEDO_CAPACITY`.
*   **Damage Balance:** `DMG_Ion Beam_BASE` (Ion Beam power), `DMG_TORPEDO` (torpedo damage).
*   **Interaction Ranges:** `DIST_MINING_MAX` (mining range), `DIST_BOARDING_MAX` (transporter range for boarding).

| Operation | Command | Maximum Distance (Sector) |
| :--- | :--- | :--- |
| **Plasma Reserves Harvest** | `har` | **3.1** |
| **Mineral Mining** | `min` | **3.1** |
| **Solar Scooping** | `sco` | **3.1** |
| **Boarding / Teams** | `bor` | **1.0** |
| **Starbase Docking** | `doc` | **3.1** |
| **Probe Recovery** | `aux recover` | **3.1** |

*Note: The operational safety distance from Black Holes is set to **3.0**. Crossing this threshold triggers gravitational pull and shield drain. The 3.1 interaction range allows for safe harvesting just outside the gravity well. A **0.05 unit tolerance** is implemented to compensate for autopilot decimal inaccuracies.*

*   **Events:** `TIMER_SUPERNOVA` (catastrophic countdown duration).

This allows admins to create custom game variants (e.g., *Hardcore Survival* with scarce resources or *Arcade Deathmatch* with overpowered weapons).

---

### 🔐 Security Architecture: "Dual-Layer" Protocol

Space GL implements a military-grade security model designed to ensure communication secrecy even in hostile multi-team environments.

#### 1. The Concept
The system uses two distinct encryption layers to balance accessibility with tactical isolation:

1.  **Master Key (Shared Secret - KEK):**
    *   **Role:** Acts as a **Key Encryption Key (KEK)**. It doesn't encrypt game data directly but secures the tunnel where the Session Key is exchanged.
    *   **Integrity Verification:** The system uses a 32-byte "Magic Signature" (`HANDSHAKE_MAGIC_STRING`). During the handshake, the server attempts to decrypt this signature using the provided Master Key.
    *   **Tactical Rigor:** If even a single bit of the Master Key differs (e.g., "ciao" vs "ciao1"), the signature will be corrupted. The server detects the anomaly and **instantly severs the TCP connection**, logging a `[SECURITY ALERT]`.
    *   **Setup:** Requested at startup by the `run_server.sh` and `run_client.sh` scripts.

2.  **Session Key (Unique Ephemeral Key):**
    *   **Role:** 256-bit random cryptographic key generated by the client for each session.
    *   **Total Isolation:** Once the Master Key is validated, the server and client switch to the Session Key. This ensures that **every player/team is tuned to a different cryptographic frequency**, making one team's data inaccessible to others even if they share the same server.

#### 2. Secure Startup Guide

To ensure security is active, always use the provided bash scripts instead of launching the executables directly.

**Starting the Server:**
```bash
./run_server.sh
# You will be asked to enter a secret Master Key (e.g., "DeltaVega47").
# This key must be shared with all authorized players.
```

**Starting the Client:**
```bash
./run_client.sh
# Enter the SAME Master Key set on the server.
# The system will confirm: "Secure Link Established. Unique Frequency active."
```

#### 3. In-Game Encryption Commands
Once connected, security is active but transparent. Captains can choose the tactical encryption algorithm (the "flavor" of encryption) using the `enc` command:

*   `enc aes`: Activates AES-256-GCM (Alliance Command Standard).
*   `enc chacha`: Activates ChaCha20-Poly1305 (High Speed).
*   `enc off`: Disables encryption (Cleartext traffic, not recommended).

*Note: If two players use different algorithms (e.g., one AES and the other ChaCha), they will not be able to read each other's radio messages, seeing only "static noise". This forces teams to coordinate communication frequencies.*

---

## 🛠️ System Architecture and Construction Details

The game is built on the **Deep Space-Direct Bridge (SDB)** architecture, a hybrid communication model designed to eliminate bottlenecks typical of real-time multiplayer simulators.

### The Deep Space-Direct Bridge (SDB) Model
This cutting-edge architecture solves latency and jitter problems typical of intensive multiplayer games by completely decoupling network synchronization from visualization fluidity. The SDB model transforms the client into an **Intelligent Tactical Relay**, optimizing remote traffic and zeroing out local latency.

1.  **Deep Space Channel (TCP/IP Binary Link)**:
    *   **Role**: Authoritative synchronization of the galactic state.
    *   **Technology**: Proprietary binary protocol with dynamic **Interest Management**. The server calculates which objects are visible to the player and sends only necessary data, reducing bandwidth usage by up to 85%.
    *   **Features**: Implements variable-length packets and binary packing (`pragma pack(1)`) to eliminate padding and maximize efficiency on remote channels.

2.  **Direct Bridge (POSIX Shared Memory Link)**:
    *   **Role**: Zero-latency interface between local logic and the graphics engine.
    *   **Technology**: Shared memory segment (`/dev/shm`) mapped directly into the address spaces of the Client and Viewer.
    *   **Efficiency**: Uses a **Zero-Copy** approach. The Client writes data received from the server directly into SHM; the 3D Viewer consumes it instantly. Synchronization is guaranteed by POSIX semaphores and mutexes, allowing the graphics engine to run at a constant 60+ FPS, applying **Linear Interpolation (LERP)** to compensate for time gaps between network packets.

#### 🔄 Data Flow Pipeline (Tactical Propagation)
The effectiveness of the SDB model is visible by observing the journey of a single update (e.g., the movement of a Xylari Warbird):
1.  **Server Tick (Logic)**: The server calculates the enemy's new global position and updates the spatial index.
2.  **Deep Space Pulse (Network)**: The server serializes the data into `PacketUpdate`, truncates it to include only objects in the player's quadrant, and sends it via TCP.
3.  **Client Relay (Async)**: The client's `network_listener` thread receives the packet, validates the `Frame ID`, and writes coordinates to **Shared Memory**.
4.  **Direct Bridge Signal (IPC)**: The client increments the `data_ready` semaphore.
5.  **Viewer Wake-up (Rendering)**: The viewer exits the *wait* state, acquires the mutex, copies the new coordinates as `target`, and starts LERP calculation to smoothly glide the vessel to the new position during subsequent graphic frames.

Thanks to this pipeline, terminal commands travel in "Deep Space" with TCP security, while the tactical view on the bridge remains stable, fluid, and stutter-free, regardless of internet connection quality.

### 1. The Galactic Server (`stellar_server`)
This is the game's "engine". It manages the entire universe of 1000 quadrants.
*   **Modular Logic**: Divided into modules (`galaxy.c`, `logic.c`, `net.c`, `commands.c`) to ensure maintainability and thread-safety.
*   **Centralized Configuration**: All balancing parameters (mining distances, weapon damage, buoy and nebula ranges) are now gathered in `include/game_config.h`. This allows admins to modify the entire game physics from a single point.
*   **Security & Stability**: The codebase has been audited to eliminate Buffer Overflow risks by systematically using `snprintf` and dynamic tactical buffer management.
*   **Spatial Partitioning**: Uses a 3D spatial index (Grid Index) for object management. This allows the server to scan only objects local to the player, guaranteeing constant performance ($O(1)$) regardless of the total number of entities in the galaxy.
*   **Persistence**: Saves the state of the entire universe, including player progress, in `galaxy.dat` with binary version control.

### 2. The Command Bridge (`stellar_client`)
The `stellar_client` represents the operational core of the user experience, acting as a sophisticated orchestrator between the human operator, the remote server, and the local rendering engine.

*   **Multi-Threaded Architecture**: The client simultaneously manages several data pipelines:
    *   A dedicated thread (**Network Listener**) constantly monitors the *Deep Space Channel*, processing incoming packets from the server without blocking the interface.
    *   The main thread handles user input and immediate feedback on the terminal.
*   **Reactive Input Management (Reactive UI)**: Thanks to `termios` `raw` mode, the client intercepts individual keystrokes in real-time. This allows the player to receive radio messages, computer alerts, and tactical updates *while typing* a command, without the cursor or text being interrupted or garbled.
*   **Direct Bridge Orchestration**: The client is responsible for the shared memory (SHM) lifecycle. At startup, it creates the memory segment, initializes synchronization semaphores, and launches the `stellar_3dview` process. Whenever it receives a `PacketUpdate` from the server, the client instantly updates the object matrix in SHM, notifying the viewer via POSIX signals.
*   **Identity & Persistence Hub**: Manages the login procedure and faction/class selection, interfacing with the server's persistent database to restore mission state.

In summary, `stellar_client` transforms a simple text terminal into an advanced and fluid command bridge, typical of an GDIS interface.

### 3. The Galactic Viewer (`spacegl_viewer`)
The `spacegl_viewer` is a low-level diagnostic tool designed for administrators and advanced players to inspect the state of the persistent galaxy stored in `galaxy.dat`.

*   **Offline Inspection**: Unlike the 3D viewer, which requires a running server and client, the `spacegl_viewer` reads the binary `galaxy.dat` file directly.
*   **Security Monitoring**: Provides a detailed report on the galaxy's cryptographic status, including HMAC-SHA256 signature verification and active encryption flags.
*   **Detailed Statistics**: Displays global counts for all 17 classes of objects (NPCs, Stars, Planets, Monsters, etc.) and player metrics.
*   **Astrometrics commands**:
    *   `stats`: Comprehensive galaxy-wide census and security report.
    *   `map <q3>`: Generates a 2D ASCII slice of the galaxy at a specific Z-depth (1-10), showing spatial distribution of nebulae, rifts, platforms, and stars.
    *   `list <q1> <q2> <q3>`: Provides a pinpoint census of a single quadrant, revealing precise coordinates, resource types (e.g., Aetherium, Neo-Titanium), and ship classes for all entities.
    *   `players`: Lists all persistent commanders, their current sector, cloaking status, and active cryptographic frequency.
    *   `search <name>`: Performs a recursive search to locate a specific captain or vessel across the entire 1000-quadrant universe.

### 4. The 3D Tactical View (`stellar_3dview`)
The 3D viewer is a standalone rendering engine based on **OpenGL and GLUT**, designed to provide an immersive spatial representation of the surrounding tactical area and the entire galaxy.

*   **Widescreen Experience (16:9)**: The display window is optimized for 1280x720 resolution, offering a cinematic field of view.
*   **Dynamic FOV**: The system automatically adjusts the Field of View: fixed at 45° in tactical mode for maximum maneuver precision, and wide-angle at 65° in Bridge mode for total immersion.
*   **Dynamic AR Compass (Augmented Reality)**: The `axs` visual system now features an advanced inertial navigation suite:
    *   **Tilting Heading Ring**: The horizontal compass ring is now locked to the ship's flight plane, tilting with pitch to maintain a constant directional reference in the pilot's field of view.
    *   **Vertical Mark Arc**: The vertical degree arc remains anchored to the galactic zenith, allowing for precise reading of the ship's effective pitch (climb/dive) as the nose slides along the arc's scale.
    *   **Fixed Galactic Axes**: X, Y, and Z axes remain absolute reference points unaffected by ship movement.
*   **Cinematic Fluidity (LERP)**: To overcome the discrete nature of network packets, the engine implements **Linear Interpolation (LERP)** algorithms for both positions and orientations (Heading/Mark). Objects don't "jump" from point to point but glide smoothly through space, maintaining 60 FPS even if the server updates logic at a lower frequency.
*   **High-Performance Rendering**: Uses **Vertex Buffer Objects (VBO)** to handle thousands of background stars and the galactic grid, minimizing CPU calls and maximizing GPU throughput.
*   **Stellar Cartography (Map Mode)**:
    *   Activatable via the `map` command, this mode transforms the tactical view into a global 10x10x10 galactic map.
    *   **Holographic Object Legend**: The map provides a high-resolution holographic projection of the sector, using specific symbols and chromatic coding to identify entities at a glance:
        *   🚀 **Player** (Cyan): Your vessel.
        *   ☀️ **Star** (Yellow): Variable spectral class.
        *   🪐 **Planet** (Cyan): Mineral or habitable resources.
        *   🛰️ **Starbase** (Green): Safe harbor for repairs.
        *   🕳️ **Black Hole** (Purple): Gravitational singularity.
        *   🌫️ **Nebula** (Grey): Gas cloud (sensor interference).
        *   ✴️ **Pulsar** (Orange): Neutron star (radiation).
        *   ☄️ **Comet** (Light Blue): Icy body in eccentric orbit.
        *   🪨 **Asteroid** (Brown): Navigable debris field.
        *   🛸 **Derelict** (Dark Grey): Abandoned ship for dismantling.
        *   💣 **Mine** (Red): Proximity explosive.
        *   📍 **Buoy** (Blue): Navigation transponder.
        *   🛡️ **Platform** (Dark Orange): Automated static defense.
        *   🌀 **Rift** (Cyan): Unstable spatial anomaly (teleport).
        *   👾 **Space Monster** (Pulsing White): Omega-class threat.
        *   ⚡ **Ion Storm** (White Wireframe): Local energy perturbation.
    *   Active **ion storms** are visualized as white energy shells surrounding the quadrant.
    *   The player's current position is highlighted by a **pulsing white indicator**, facilitating long-range navigation.
*   **Dynamic Tactical HUD**: Implements a 2D-on-3D projection (via `gluProject`) to anchor labels, health bars, and IDs directly above vessels. The overlay now includes real-time monitoring of **Crew (CREW)**, vital for mission survival.
*   **Effects Engine (VFX)**:
    *   **Trail Engine**: Each ship leaves a persistent ionic trail that helps visualize its movement vector.
    *   **Combat FX**: Real-time visualization of Ion Beam beams managed via **GLSL Shaders**, Plasma Torpedoes with dynamic glow, and volumetric explosions.
    *   **Dismantle Particles**: A dedicated particle system animates the dismantling of enemy wrecks during resource recovery operations.
*   **3D Tactical Cube**: The wireframe grid surrounding the sector is now vertically aligned with the depth levels (S3) of the `lrs` command:
    *   🟩 **Upper Plane (Green)**: Corresponds to `[ LONG RANGE DEPTH +1 ]` (Upper altitude).
    *   🟨 **Center (Yellow)**: Corresponds to `[ LOCAL TACTICAL ZONE 0 ]` (Your current altitude).
    *   🟥 **Lower Plane (Red)**: Corresponds to `[ LONG RANGE DEPTH -1 ]` (Lower altitude).
    *   This mapping allows for instant vertical identification of objects.

The Tactical View is not just an aesthetic element but a fundamental tool for short-range combat and precision navigation between celestial bodies and environmental threats.

---

## 📡 Communication Protocols

### Network (Server ↔ Client): The Deep Space Channel
Remote communication is entrusted to a custom state-aware binary protocol, designed to ensure consistency and performance on networks with variable latency.

*   **Deterministic Binary Protocol**: Unlike text protocols (like JSON or XML), the Deep Space Channel uses a **Binary-Only** architecture. Data structures are aligned via `pragma pack(1)` to eliminate compiler padding, ensuring every transmitted byte is useful information.
*   **State-Aware Synchronization**:
    *   The server doesn't just send positions but synchronizes the entire logical state needed by the client (energy, shields, inventory, onboard computer messages).
    *   Every update packet (`PacketUpdate`) includes a global **Frame ID**, allowing the client to correctly handle the temporal order of data.
*   **Interest Management & Delta Optimization**:
    *   **Spatial Filtering**: The server dynamically calculates each player's visibility set. You will only receive data on objects present in your current quadrant or affecting your long-range sensors, drastically reducing network load.
    *   **Truncated Updates**: Packets containing object lists (like enemy ships or debris) are physically truncated before sending. If there are only 2 ships in your quadrant, the server will send a packet containing only those 2 slots instead of the entire fixed array, saving precious KB every tick.
*   **Data Integrity & Stream Robustness**:
    *   Implements an **Atomic Read/Write** mechanism. The `read_all` and `write_all` functions ensure that, despite the "stream" nature of TCP, binary packets are reconstructed only when complete and intact, preventing logical state corruption during traffic spikes.
*   **Signal Multiplexing**: The protocol manages different packet types (`Login`, `Command`, `Update`, `Message`, `Query`) on the same socket, acting as a Deep Space signal multiplexer.

This implementation allows the simulator to scale smoothly, keeping command latency (Input Lag) minimal and galaxy consistency absolute for all connected captains.

### IPC (Client ↔ Viewer): The Direct Bridge
The link between the command bridge and the tactical view is realized via an inter-process communication (IPC) interface based on **POSIX Shared Memory**, designed to eliminate local data exchange latency.

*   **Shared-Memory Architecture**: The `stellar_client` allocates a dedicated memory segment (`/st_shm_PID`) where the `GameState` structure resides. This structure acts as a mirror representation of the local state, accessible in real-time by both the client (writer) and the viewer (reader).
*   **Hybrid Synchronization (Mutex & Semaphores)**:
    *   **PTHREAD_PROCESS_SHARED Mutex**: Data consistency within shared memory is guaranteed by a mutex configured for cross-process use. This prevents the viewer from reading partial data while the client is updating the object matrix.
    *   **POSIX Semaphores**: A semaphore (`sem_t data_ready`) is used to implement a "Producer-Consumer" notification mechanism. Instead of constantly polling memory, the viewer remains in an efficient wait state until the client signals the availability of a new logical frame.
*   **Zero-Copy Efficiency**: Since data physically resides in the same RAM area mapped into both address spaces, passing telemetry parameters involves no additional memory copy (memcpy), maximizing system bus performance.
*   **Event Latching**: SHM handles "latching" of rapid events (like explosions or Ion Beam discharges). The client deposits the event in memory, and the viewer, after rendering it, resets the flag, ensuring no tactical effect is lost or duplicated.
*   **Lifecycle Orchestration**: The client acts as a supervisor, managing creation (`shm_open`), sizing (`ftruncate`), and final destruction of the IPC resource, ensuring the system leaves no memory orphans in case of a crash.

This approach transforms the 3D viewer into a pure reactive graphics slave, allowing the rendering engine to focus exclusively on visual fluidity and geometric calculation.

---

## 🔍 Technical Specifications

Space GL is not just a tactical simulator but a complex software architecture implementing advanced design patterns for distributed state management and real-time calculation.

### 1. The Synaptics Logic Engine (Tick-Based Simulation)
The server operates on a deterministic loop at **30 Ticks Per Second (TPS)**. Each logic cycle follows a rigorous pipeline:
*   **Input Reconciliation**: Processing of atomic commands received from clients via epoll.
*   **Predictive AI Update**: Calculation of movement vectors for NPCs based on pursuit matrices and tactical weights (faction, residual energy).
*   **Spatial Indexing (Grid Partitioning)**: Objects are not iterated linearly ($O(N)$), but mapped into a 10x10x10 three-dimensional grid. This reduces collision and sensor complexity to ($O(1)$) for the player's local area.
*   **Physics Enforcement**: Application of galactic clamping and collision resolution with static celestial bodies.

### 2. ID-Based Object Tracking & Shared Memory Mapping
The object tracking system uses a **Persistent Identifier** architecture:
*   **Server Side**: Each entity (ship, star, planet) has a unique global ID. During the tick, only IDs visible to the player are serialized.
*   **Client/Viewer Side**: The viewer maintains a local buffer of 200 slots. Through an **implicit Hash Map**, the client associates the server ID with an SHM slot. If an ID disappears from the network packet, the *Stale Object Purge* system instantly invalidates the local slot, ensuring visual consistency without timeout latency.

### 3. Advanced Networking Model (Atomic Binary Stream)
To ensure stability under high-frequency updates, the simulator implements:
*   **Atomic Packet Delivery:** Each client connection is protected by a dedicated `socket_mutex`. This ensures that large data packets (like the Galaxy Master) are never interleaved with logic update packets, eliminating binary stream corruption and race conditions.
*   **Binary Layout Consistency:** All network structures exclusively use fixed-size types (`int32_t`, `int64_t`, `float`). Combined with `pragma pack(1)`, this ensures the protocol is identical across different CPU architectures and compiler versions.
*   **Synchronous Security Handshake:** Connections are not established blindly. The server performs synchronous validation of a 32-byte signature before sending any sensitive data, providing a robust logical firewall.

### 4. Memory Management & Server Stability
*   **Heap-Based Command Processing:** Sensor scan functions (`srs`, `lrs`) utilize dynamic Heap allocation (`calloc`/`free`) to handle data buffers exceeding 64KB. This prevents *Stack Overflow* risks, ensuring server stability even in ultra-high density quadrants.
*   **Zero-Copy Shared Memory (Realigned):** IPC now uses explicit sector coordinates (`shm_s`) for player positioning, eliminating ID-lookup latency in the 3D viewer and ensuring pixel-perfect camera tracking.

### 5. GLSL Rendering Pipeline (Hardware-Accelerated Aesthetics)
The 3D viewer implements a programmable shading pipeline:
*   **Vertex Stage**: Handling of HUD projection transformations and per-pixel light vector calculation.
*   **Fragment Stage**:
    *   **Aztek Shader**: Procedural generation of hull textures based on fragment coordinates, eliminating the need for external graphic assets.
    *   **Fresnel Rim Lighting**: Calculation of the dot product between normal and view vector to highlight vessel structural profiles.
    *   **Plasma Flow Simulation**: Temporal animation of emissive parameters to simulate energy flow in Hyperdrive nacelles.

### 6. Robustness and Session Continuity
*   **Atomic Save-State**: The `galaxy.dat` database is updated via periodic flushes with global mutex locking, ensuring a consistent memory snapshot.
*   **Emergency Rescue Protocol**: A heuristic rescue logic intervenes at login to resolve error states (collisions or destroyed ships), ensuring the persistence of the player's career even in case of mission failure.

---

## 🌌 Galactic Encyclopedia: Entities and Phenomena

The Space GL universe is a dynamic ecosystem populated by 17 classes of entities, each with unique physical, tactical, and visual properties.

### 🌟 Celestial Bodies and Astronomical Phenomena
*   **Stars**: Classified into 7 spectral types (O, B, A, F, G, K, M). They provide energy via *Solar Scooping* (`sco`) but can become unstable and trigger a **Supernova**.
    *   **Frequency**: Globally synchronized cataclysmic events (approx. every **5 minutes** of gameplay).
    *   **Alert**: A countdown appears in the HUD when a star is imminent to explode.
    *   **Impact**: Total destruction of all vessels in the quadrant.
    *   **Aftermath**: The star is replaced by a permanent **Black Hole**, forever changing the galactic map.
*   **Planets**: Celestial bodies rich in minerals. They can be scanned and mined (`min`) for Aetherium, Neo-Titanium, and other vital resources.
*   **Black Holes**: Gravitational singularities with accretion disks. They are the primary source of Plasma Reserves (`har`), but their pull can be fatal.
*   **Nebulas**: Large clouds of ionized gases (Standard, High-Energy, Dark Matter, etc.). They provide tactical cover (natural cloaking) but disrupt sensors.
*   **Pulsars**: Neutron stars emitting deadly radiation. Navigating too close damages systems and crew.
*   **Comets**: Fast-moving objects with volumetric tails. They can be analyzed to collect rare gases.
*   **Spatial Rifts**: Tears in the spacetime fabric. They act as natural teleporters that project the ship to a random point in the galaxy.

### 🚩 Factions and Intelligent Ships
*   **Alliance (Player/Starbase)**: Includes your ship and Starbases, where you can dock (`doc`) for full repairs and resupply.
*   **Korthian Empire**: Aggressive warriors patrolling quadrants, often protected by defense platforms.
*   **Xylari Star Empire**: Masters of deception using cloaking devices to launch surprise attacks.
*   **Swarm Collective**: The greatest threat. Their Cubes have massive firepower and superior regenerative capabilities.
*   **NPC Factions**: Vesperians, Ascendant, Quarzites, Saurian, Gilded, Fluidic Void, Cryos, and Apex. Each with varying levels of hostility and power.

#### ⚖️ Faction System and "Renegade" Protocol
Space GL implements a dynamic reputation system that manages relationships between the player and the various galactic powers.

*   **Allied IFF Recognition**: Upon character creation, the captain chooses a faction. NPC ships and defense platforms of the same faction will recognize the vessel as an ally and **will not open fire on sight**.
*   **Friendly Fire and Betrayal**: If a player deliberately attacks a unit of their own faction (ship, base, or platform):
    *   **Renegade Status**: The captain is immediately marked as a **TRAITOR (Renegade)**.
    *   **Immediate Retaliation**: All units of their own faction in the sector will become hostile and begin heavy attack maneuvers.
    *   **Alert Message**: The onboard computer will receive a critical warning: `FRIENDLY FIRE DETECTED! You have been marked as a TRAITOR by the fleet!`.
*   **Duration and Amnesty**: The traitor status is severe but temporary. It lasts for **10 minutes (real time)**.
    *   During this period, any further attack against allies will reset the timer.
    *   When the timer expires, if no further hostilities have been committed, Sector Command will grant amnesty: `Amnesty granted. Your status has been restored to active duty.` and faction units will return to being neutral/allied.

### ⚠️ Tactical Hazards and Resources
*   **Asteroid Fields**: Rocky debris posing a physical risk. Collision damage increases with ship speed.
*   **Space Mines**: Hidden explosive devices placed by hostile factions. Detectable only via close-range scanning.
*   **Derelict Ships**: Hulls of destroyed vessels. Can be dismantled (`dis`) to recover components and resources.
*   **Communication Buoys**: Alliance Command network nodes. Being nearby enhances long-range sensors (`lrs`), providing detailed composition data on adjacent quadrants.
*   **Defense Platforms (Turrets)**: Heavily armed automated sentinels protecting strategic areas of interest.

### 👾 Anomalies and Creatures
*   **Space Monsters**: Includes the **Crystalline Entity** and the **Space Amoeba**, unique creatures that actively hunt vessels to feed on their energy.
*   **Ion Storms**: Meteorological phenomena moving through the galaxy, capable of blinding sensors and diverting ships' courses.

---

## 🕹️ Operational Command Manual

Below is the complete list of available commands, grouped by function.

### 🚀 Navigation
*   `nav <H> <M> <Dist> [Factor]`: **High-Precision Hyperdrive Navigation**. Set course, precise distance and optional velocity.
    *   `H`: Heading (0-359).
    *   `M`: Mark (-90 to +90).
    *   `Dist`: Distance in Quadrants (supports decimals, e.g. `1.73`).
    *   `Factor`: (Optional) Hyperdrive Factor from 1.0 to 9.9 (Default: 6.0).
*   `imp <H> <M> <S>`: **Impulse Drive**. Sub-light engines. `S` represents speed from 0.0 to 1.0 (Full Impulse).
    *   `S`: Speed (0.0 - 1.0).
    *   `imp 0 0 0`: All Stop.
*   `cal <QX> <QY> <QZ> [SX SY SZ]`: **Navigational Computer (High Precision)**. Generates a full report with Heading, Mark, and a **Velocity Comparison Table**. If sector coordinates (SX, SY, SZ) are provided, it calculates the pinpoint route to that specific location.
*   `ical <X> <Y> <Z>`: **Impulse Calculator (ETA)**. Calculates H, M, and ETA to reach precise coordinates (0.0-10.0) within the current quadrant, based on current engine power allocation.
    
    *   `jum <QX> <QY> <QZ>`: **Wormhole Jump (Einstein-Rosen Bridge)**.
     Generates a wormhole for an instant jump to the destination quadrant.
    *   **Requirements**: 5000 units of Energy and 1 Aetherium Crystal.
    *   **Procedure**: Requires a singularity stabilization sequence of about 3 seconds.
*   `apr [ID] [DIST]`: **Approach Autopilot**. Automatic approach to target ID up to DIST distance.
    *   If no ID is provided, it uses the currently **locked target**.
    *   If only one number is provided, it is treated as **distance** for the locked target (if < 100).
*   `cha`: **Chase Autopilot**. Actively chases the locked target, maintaining intercept trajectory.
*   `rad <MSG>`: **Deep Space Radio**. Sends a global message. Use `@Faction` for team chat or `#ID` for private messages.
*   `doc`: **Docking**. Dock at a Starbase (requires close range).
*   `map [FILTER]`: **Stellar Cartography**. Activates global 10x10x10 3D visualization of the entire galaxy.
    *   **Optional Filters**: You can display only specific categories using: `map st` (Stars), `map pl` (Planets), `map bs` (Bases), `map en` (Enemies), `map bh` (Black Holes), `map ne` (Nebulas), `map pu` (Pulsars), `map is` (Storms), `map co` (Comets), `map as` (Asteroids), `map de` (Derelicts), `map mi` (Mines), `map bu` (Buoys), `map pf` (Platforms), `map ri` (Rifts), `map mo` (Monsters).
    *   **Vertical HUD**: In map mode, a legend on the left side shows colors and filter codes for each object.
    *   **Dynamic Anomalies**:
        *   **Ion Storms**: Quadrants enclosed in a white transparent wireframe shell.
        *   **Supernova**: A **large red pulsing cube** indicates an imminent stellar explosion in the quadrant (extreme hazard).
    *   **Localization**: Current ship position is indicated by a **pulsing white cube**.

### 🔬 Sensors and Scanners
*   `scan <ID>`: **Deep Scan Analysis**. Performs a deep scan of the target or anomaly.
    *   **Vessels**: Reveals hull integrity, shield levels per quadrant, residual energy, crew count, and subsystem damage.
    *   **Anomalies**: Provides scientific data on Nebulas and Pulsars.

#### 📡 Sensor Integrity and Data Accuracy
The effectiveness of your sensors depends directly on the health of the **Sensors system (ID 2)**:
*   **100% Health**: Accurate and reliable data.
*   **Health < 100%**: Introduction of telemetry "noise". Sector coordinates `[X,Y,Z]` shown in `srs` become imprecise (error increases exponentially as health drops).
*   **Health < 50%**: The `lrs` command starts displaying corrupted or incomplete data about surrounding quadrants.
*   **Health < 30%**: Risk of "Target Ghosting" or failing to detect real objects.
*   **Repair**: Use `rep 2` to restore nominal sensor precision.

*   `srs`: **Short Range Sensors**. Detailed scan of the current quadrant.
    *   **Neighborhood Scan**: If the ship is near sector boundaries (< 2.5 units), sensors automatically detect objects in adjacent quadrants, listing them in a dedicated section to prevent ambushes.
*   `lrs`: **Long Range Sensors**. 3x3x3 scan of surrounding quadrants displayed via **GDIS Tactical Console**.
    *   **Layout**: Each quadrant is displayed on a single line for immediate readability (Coordinates, Navigation, Objects, and Anomalies).
    *   **Standard Data**: Shows only object presence using their initials (e.g., `[H . N . S]`).
    *   **Enhanced Data**: If the ship is near a **Communication Buoy** (< 1.2 units), sensors switch to numeric display revealing the exact count (e.g., `[1 . 2 . 8]`). The boost resets when moving away from the buoy.
    *   **Navigation Solution**: Each quadrant includes `H / M / W` parameters calculated to reach it immediately.
    *   **Primary Legend**: `[ H P N B S ]` (Black Holes, Planets, NPCs, Bases, Stars).
    *   **Anomaly Symbols**: `~`:Nebula, `*`:Pulsar, `+`:Comet, `#`:Asteroid, `M`:Monster, `>`:Rift.
    *   **Localization**: Your current quadrant is highlighted with a blue background.
*   `aux probe <QX> <QY> <QZ>`: **Deep Space Sensor Probe**. Launches an automated probe to a specific quadrant.
    *   **Galactic Entity**: Probes are global objects. They traverse intermediate quadrants in real-time and are **visible to all players** along their flight path.
    *   **Sensor Integration**: Probes appear in the **SRS (Short Range Sensors)** list for any ship in the same sector (ID range 19000+), revealing the **Owner's Name** and current status.
    *   **Functionality**: Displays **ETA** and mission status in the owner's HUD.
    *   **Data Retrieval**: Upon arrival, it reveals the quadrant's composition (`H P N B S`) in the owner's map and sends a live telemetry report.
    *   **Persistence**: Remains at the target location as a **Derelict** (Red rings) after the mission.
    *   **Command `aux report <1-3>`**: Requests a fresh sensor update from an active probe.
    *   **Command `aux recover <1-3>`**: Recovers a probe if the ship is in the same quadrant and within range (< 2.0 units), freeing the slot and restoring 500 energy units.
*   `sta`: **Status Report**. Complete report on ship state, mission, and **Crew** monitoring.
*   `dam`: **Damage Report**. Detailed system damage.
*   `who`: List of active captains in the galaxy.

### ⚔️ Tactical Combat
*   `pha <E>`: **Fire Ion Beams**. Fires Ion Beams at the locked target (`lock`) using energy E. 
*   `pha <ID> <E>`: Fires Ion Beams at a specific target ID. Damage decreases with distance.
*   `cha`: **Chase**. Automatically chases and intercepts the locked target.
*   `rad <MSG>`: **Radio**. Sends a Deep Space message to other captains (@Faction for team chat).
*   `axs` / `grd`: **Visual Guides**. Toggles 3D axes or tactical grid overlay.
*   `bridge [top/bottom/up/down/left/right/rear/off]`: **Bridge View**. Toggles a cinematic first-person view.
    *   `top/on`: Standard bridge view above the command dome.
    *   `bottom`: Under-hull perspective.
    *   `up/down/left/right/rear`: Changes looking direction while maintaining the current position (top/bottom).
*   `enc <algo>`: **Encryption Toggle**. Enables or disables encryption in real-time. Supports **AES-256-GCM**, **ChaCha20**, **ARIA**, **Camellia**, **Blowfish**, **RC4**, **CAST5**, **IDEA**, **3DES**, and **PQC (ML-KEM)**. Essential for protecting communications and reading secure messages from other captains.
*   `tor`: **Fire Plasma Torpedo**. Launches an auto-guided torpedo at the locked target.
*   `tor <H> <M>`: Launches a torpedo in manual ballistic mode (Heading/Mark).
*   `lock <ID>`: **Target Lock**. Locks targeting systems onto target ID (0 to unlock). Essential for automated Ion Beam and torpedo guidance.


### 🆔 Galactic Identifier Schema (Universal ID)
To interact with galactic objects using the `lock`, `scan`, `pha`, `tor`, `bor`, and `dis` commands, the system employs a unique ID system. Use the `srs` command to identify the IDs of objects in your sector.

| Category | ID Range | Example | Primary Usage |
| :--- | :--- | :--- | :--- |
| **Player** | 1 - 999 | `lock 1` | Your vessel or other players |
| **NPC (Enemy)** | 1,000 - 1,999 | `lock 1050` | Chasing (`cha`) and combat |
| **Starbases** | 2,000 - 2,999 | `lock 2005` | Docking (`doc`) and resupply |
| **Planets** | 3,000 - 3,999 | `lock 3012` | Planetary mining (`min`) |
| **Stars** | 4,000 - 6,999 | `lock 4500` | Solar scooping (`sco`) |
| **Black Holes** | 7,000 - 7,999 | `lock 7001` | Plasma Reserves harvest (`har`) |
| **Nebulas** | 8,000 - 8,999 | `lock 8000` | Scientific analysis and cover |
| **Pulsars** | 9,000 - 9.999 | `lock 9000` | Radiation monitoring |
| **Comets** | 10,000 - 10,999| `lock 10001` | Chasing and rare gas collection |
| **Derelicts** | 11,000 - 11,999| `lock 11005` | Boarding (`bor`) and tech recovery |
| **Asteroids** | 12,000 - 13,999| `lock 12000` | Precision navigation |
| **Mines** | 14,000 - 14,999| `lock 14000` | Tactical alert and avoidance |
| **Comm Buoys** | 15,000 - 15,999| `lock 15000` | Data link and `lrs` boost |
| **Platforms** | 16,000 - 16,999| `lock 16000` | Destroying hostile sentinels |
| **Spatial Rifts** | 17,000 - 17,999| `lock 17000` | Use for random jumps |
| **Monsters** | 18,000 - 18,999| `lock 18000` | Extreme combat scenarios |
| **Probes** | 19,000 - 19,999| `scan 19000` | Automated data collection |

**Note**: Locking only works if the object is in your current quadrant. If the ID exists but is far away, the computer will indicate the target's `Q[x,y,z]` coordinates.

### 🔄 Recommended Tactical Workflow
To perform complex operations (mining, resupply, boarding), follow this optimized sequence:

1.  **Identification**: Use `srs` to find the object ID (e.g., Star ID `4226`).
2.  **Lock-on**: Execute `lock 4226`. You will see the ID confirmed on your 3D HUD.
3.  **Approach**: Use `apr 4226 1.5`. The autopilot will bring you to the ideal interaction distance.
4.  **Interaction**: Once you arrive, launch the specific command:
    *   `sco` for **Stars** (Energy recharge).
    *   `min` for **Planets** (Mineral mining).
    *   `har` for **Black Holes** (Plasma Reserves harvest).
    *   `bor` for **Derelicts** (Tech recovery and repairs).
    *   `cha` for **Comets** (Chase and gas collection).
    *   `pha` / `tor` for **Enemies/Monsters/Platforms** (Combat).

### 📏 Interaction Distances Table
Distances expressed in sector units (0.0 - 10.0). If your distance is greater than the limit, the computer will respond with "No [object] in range".

| Object / Entity | Command / Action | Minimum Distance | Effect / Interaction |
| :--- | :--- | :--- | :--- |
| **Star** | `sco` | **< 2.0** | Solar scooping (energy recharge) |
| **Planet** | `min` | **< 2.0** | Planetary mining |
| **Starbase** | `doc` | **< 2.0** | Full repair, energy and torpedo refill |
| **Black Hole** | `har` | **< 2.0** | Plasma Reserves harvesting |
| **Derelict** | `dis` | **< 1.5** | Dismantling for resources |
| **Enemy Ship** | `bor` | **< 1.0** | Boarding party operation |
| **Enemy Ship** | `pha` (Fire) | **< 6.0** | Maximum NPC Ion Beam range |
| **Plasma Torpedo** | (Impact) | **< 0.5** | Collision distance for detonation |
| **Comm Buoy** | (Passive) | **< 1.2** | Signal boost or auto-messages |
| **Space Amoeba** | (Contact) | **< 1.5** | Critical energy drain start |
| **Crystalline E.** | (Resonance) | **< 4.0** | Range of the resonance beam |
| **Celestial Body** | (Collision) | **< 1.0** | Hull damage and emergency rescue trigger |

### 🚀 Autopilot (`apr`)
The `apr <ID> <DIST>` command allows you to automatically approach any object detected by sensors. For mobile entities, interception works across the entire galaxy.

| Object Category | ID Range | Interaction Commands | Min. Distance | Navigation Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Captains (Players)** | 1 - 32 | `rad`, `pha`, `tor`, `bor` | **< 1.0** (`bor`) | Galactic Tracking |
| **NPC Ships (Aliens)** | 1000 - 1999 | `pha`, `tor`, `bor`, `scan` | **< 1.0** (`bor`) | Galactic Tracking |
| **Starbases** | 2000 - 2199 | `doc`, `scan` | **< 2.0** | Current quadrant only |
| **Planets** | 3000 - 3999 | `min`, `scan` | **< 3.1** | Current quadrant only |
| **Stars** | 4000 - 6999 | `sco`, `scan` | **< 2.0** | Current quadrant only |
| **Black Holes** | 7000 - 7199 | `har`, `scan` | **< 2.0** | Current quadrant only |
| **Nebulas** | 8000 - 8499 | `scan` | - | Current quadrant only |
| **Pulsars** | 9000 - 9199 | `scan` | - | Current quadrant only |
| **Comets** | 10000 - 10299 | `cha`, `scan` | **< 0.6** (Gas) | **Galactic Tracking** |
| **Derelicts** | 11000 - 11149 | `bor`, `dis`, `scan` | **< 1.5** | Current quadrant only |
| **Asteroids** | 12000 - 13999 | `min`, `scan` | **< 3.1** | Current quadrant only |
| **Mines** | 14000 - 14999 | `scan` | - | Current quadrant only |
| **Comm Buoys** | 15000 - 15099 | `scan` | **< 1.2** | Current quadrant only |
| **Defense Platforms** | 16000 - 16199 | `pha`, `tor`, `scan` | - | Current quadrant only |
| **Spatial Rifts** | 17000 - 17049 | `scan` | - | Current quadrant only |
| **Space Monsters** | 18000 - 18029 | `pha`, `tor`, `scan` | **< 1.5** | **Galactic Tracking** |

*   `she <F> <R> <T> <B> <L> <RI>`: **Shield Configuration**. Distributes energy to the 6 shields.
*   `clo`: **Cloaking Device**. Activates/Deactivates cloak. Consumes 15 energy units/tick. Provides invisibility to NPCs and other factions; unstable in nebulas.
*   `pow <E> <S> <W>`: **Power Allocation**. Allocates reactor energy (Engines, Shields, Weapons %).
*   `aux jettison`: **Eject Hyperdrive Synaptics**. Ejects the core (Suicide maneuver / Last resort).
*   `xxx`: **Self-Destruct**. Sequential self-destruction.

### ⚡ Reactor and Power Management

The `pow` command is critical for survival and tactical superiority. It dictates how the ship's main reactor output is partitioned across three core subsystems:

*   **Engines (E)**: Affects **Impulse Drive** responsiveness and top speed. High allocation allows for rapid maneuvers and faster sector traversal.
*   **Shields (S)**: Governs the **Regeneration Rate** of all 6 shield quadrants. If shields are damaged, they draw energy from the reactor to rebuild their integrity.
    *   **Dynamic Scaling**: The regeneration speed is a product of both the assigned **Power (S)** and the **Shield System Integrity**. If the shield generator is damaged, regeneration will be severely hampered regardless of power allocation.

#### 🛡️ Shield Mechanics and Hull Integrity
The ship is protected by 6 independent quadrants: **Front (F), Rear (R), Top (T), Bottom (B), Left (L), and Right (RI)**.

*   **Localized Damage**: Attacks (Ion Beams/Torpedoes) now hit specific quadrants based on the relative angle of impact.
*   **Hull Integrity**: Represents the physical health of the ship (0-100%). If a shield quadrant reaches 0% or the impact is excessively powerful, residual damage directly hits the structural integrity.
*   **Internal System Damage**: When the hull is struck directly (shields at zero), there is a high probability of sustaining damage to subsystems (engines, weapons, sensors, etc.).
    *   **Ion Beams**: Moderate chance of random system damage.
    *   **Torpedoes**: Very high chance (>50%) of critical system failure upon impact.
*   **Hull Plating (Composite)**: Additional plating (command `hull`) acts as a buffer: it absorbs physical damage *before* it affects Hull Integrity.
*   **Destruction Condition**: If **Hull Integrity reaches 0%**, the ship instantly explodes, regardless of remaining energy or shield levels.
*   **Continuous Regeneration**: Unlike older systems, shield regeneration is continuous but scales with hardware health.
*   **Shield Failure**: If a quadrant reaches 0% integrity, subsequent hits from that direction will deal direct damage to the hull and the main energy reactor.

#### 🛸 Cloaking Device
The `clo` command activates an advanced cloaking technology that manipulates light and sensors to make the vessel invisible.

*   **Tactical Invisibility**: Once active, you will not be detectable by the sensors (`srs`/`lrs`) of other players (unless they belong to your own faction). NPC ships will ignore you completely and will not initiate attack maneuvers.
*   **Energy Costs**: Maintaining the cloak field is extremely energy-intensive, consuming **15 energy units per logic tick**. Carefully monitor your reactor reserves.
*   **Sensor Limitations**: While cloaked, onboard sensors experience interference ("Sensors limited"), making it harder to acquire precise environmental data.
*   **Instability in Nebulas**: Inside nebulas, the cloak field becomes unstable due to ionized gases. This can cause fluctuations in energy drain and inhibit shield regeneration.
*   **Visual Feedback**: When cloaked, the original ship model disappears and is replaced by a wireframe mesh with a pulsing **"Blue Glowing" effect**. The HUD will display the status `[ CLOAKED ]` in magenta.
*   **Combat Restrictions**: You cannot fire **Ion Beams** (`pha`) or launch **Torpedoes** (`tor`) while the cloaking device is active. You must decloak to engage the enemy.
*   **NPC Strategy (Xylaris)**: The Xylari Star Empire uses advanced cloaking tactics; their ships will remain cloaked while patrolling or fleeing, only revealing themselves to launch an attack.

*   **Weapons (W)**: Directly scales the **Ion Beam Beam Intensity** and **Recharge Rate**. A higher allocation results in more energy being focused into the Ion Beam banks, dealing exponentially more damage and allowing the Ion Beam capacitor to refill much faster.

#### 🎯 Tactical Ordnance Note: Ion Beams and Torpedoes
*   **Ion Beam Capacitor**: Visible in the HUD as "Ion Beam CAPACITOR: XX%". This represents the energy currently stored in the weapon banks.
    *   **Firing**: Each Ion Beam burst consumes a portion of the capacitor based on the energy setting. If the capacitor is below 10%, you cannot fire.
    *   **Recharging**: Refills automatically every second. The refill speed is directly boosted by assigning more power to **Weapons (W)** via the `pow` command.
*   **Ion Beam Integrity**: In the HUD as "Ion Beam INTEGRITY: XX%". This represents hardware health. Damage is multiplied by this value. Use `rep 4` to fix.
*   **Torpedo Tubes**: Visible in the HUD as "TUBES: <STATE>".
    *   **READY**: Systems are armed and ready to fire.
    *   **FIRING...**: A torpedo is currently in flight. New launches are inhibited until impact or sector exit.
    *   **LOADING...**: Post-launch cooling and reloading sequence (approx. 5 seconds).
    *   **OFFLINE**: Hardware integrity is below 50%. Launching is impossible until the system is repaired (`rep 5`).

### 💓 Life Support and Crew Safety
The HUD displays "LIFE SUPPORT: XX.X%", which is directly linked to the integrity of the ship's vital systems.
*   **Initialization**: Every mission starts with Life Support at 100%.
*   **Critical Threshold**: If the percentage drops below **75%**, the crew will begin to suffer casualties due to environmental failure (radiation, oxygen loss, or gravity fluctuations).
*   **Emergency Repairs**: Maintaining Life Support above the threshold is the highest priority. Use `rep 7` immediately if integrity is compromised.
*   **Mission Failure**: If the crew count reaches **zero**, the vessel is declared lost, and the simulation ends.

**HUD Feedback**: The current allocation is visible in the bottom-right diagnostics panel as `POWER: E:XX% S:XX% W:XX%`. Monitoring this is essential to ensure your ship is optimized for the current mission phase (Exploration vs. Combat).

### 📦 Operations and Resources
*   `bor [ID]`: **Boarding Party**. Sends boarding parties (Dist < 1.0).
    *   Works on the currently **locked target** if no ID is specified.
    *   **NPC/Derelict Interaction**: Automatic rewards (Aetherium, Chips, Repairs, Survivors, or Prisoners).
    *   **Player-to-Player Interaction**: Opens an **Interactive Tactical Menu** with specific choices:
        *   **Allied Vessels**: `1`: Transfer Energy, `2`: Repair System, `3`: Send Crew Reinforcements.
        *   **Hostile Vessels**: `1`: Sabotage System, `2`: Raid Cargo Hold, `3`: Capture Hostages.
    *   **Selection**: Reply with the number `1`, `2`, or `3` to execute the action.
    *   **Risks**: Resistance chance (30% for players, higher for NPCs) may cause team casualties.
*   `dis`: **Dismantle**. Dismantles enemy wrecks for resources (Dist < 1.5).
*   `min`: **Mining**. Extracts resources from an orbiting planet or asteroid (Dist < 3.1).
    *   **Selective Priority**:
        1.  If a target is locked (`lock <ID>`), the system grants it absolute priority.
        2.  Without a lock, the system will mine the **absolute closest** mineable object.
    *   **Radio Feedback**:
        *   Asteroids: `[RADIO] MINING (Alliance Command): Asteroid extraction complete.`
        *   Planets: `[RADIO] GEOLOGY (Alliance Command): Planetary mining successful.`
*   `sco`: **Solar Scooping**. Collects energy from a star (Dist < 3.1).
*   `har`: **Harvest Plasma Reserves**. Collects antimatter from a black hole (Dist < 3.1).
*   `con T A`: **Convert Resources**. Converts raw materials into energy or torpedoes (`T`: resource type, `A`: amount).
    *   `1`: Aetherium -> Energy (x10).
    *   `2`: Neo-Titanium -> Energy (x2).
    *   `3`: Void-Essence -> Torpedoes (1 per 20).
    *   `6`: Gas -> Energy (x5).
    *   `7`: Composite -> Energy (x4).
    *   `8`: **Dark-Matter** -> Energy (x25). [Maximum Efficiency]. Extremely rare radioactive mineral. Convert it with `con 8 <amount>` to instantly recharge Cargo energy reserves.
*   `load <T> <A>`: **Load Systems**. Transfers energy or torpedoes from cargo to active systems.
    *   `1`: Energy (Main Reactor). Max capacity: 9,999,999 units. Allows converting harvested Plasma Reserves from Black Holes into operational energy.
    *   `2`: Torpedoes (Launch Tubes). Max capacity: 1000 units.

#### 🏗️ Hull Reinforcement (Hull Plating)
*   `hull`: **Reinforce Hull**. Uses **100 units of Composite** to apply reinforced plating to the hull (+500 integrity units).
    *   Composite plating acts as a secondary physical shield, absorbing residual damage that bypasses energy shields before it hits the main reactor.
    *   Plating status is visible in the 3D HUD and via the `sta` command.
*   `inv`: **Inventory**. Shows cargo bay content, including raw materials (**Graphene**, **Synaptics**, **Composite**) and **Prisoners**.

### 📦 Cargo and Resource Management

Space GL distinguishes between **Active Systems**, **Cargo Storage**, and the **Prison Unit**. This is reflected in the HUD as `ENERGY: X (CARGO: Y)`.



*   **Active Energy/Torps**: These are resources currently available for immediate use.

*   **Cargo Reserves (Cargo Bay)**: Resources stored for long-range replenishment.

    *   **Resource Table**:

        1. **Aetherium**: Hyperdrive Jump and energy conversion.

        2. **Neo-Titanium**: Hull repairs and energy conversion.

        3. **Void-Essence**: Material for Plasma Torpedo warheads (`[WARHEADS]`).

        4. **Graphene**: Advanced structural alloy.

        5. **Synaptics**: Chips for complex system repairs.

                6. **Nebular Gas**: Collected from comets, energy conversion.

                7. **Composite**: Armored hull plating.

                8. **Dark-Matter**: Rare radioactive mineral found in specialized asteroids and planets. Used for experimental tech and system enhancement.

*   **Prison Unit**: A dedicated unit for detaining enemy personnel captured during boarding operations. It is monitored in real-time in the vital HUD next to the crew count.

*   **Resource Conversion**: Raw materials must be converted (`con`) into **CARGO Plasma Reserves** or **CARGO Torpedoes** before loading into active systems.

*   `rep [ID]`: **Repair**. Repairs a damaged system (health < 100%) restoring it to full efficiency. Essential for fixing sensor noise or reactivating offline weapons.
    *   **Cost**: Each repair consumes **50 Neo-Titanium** and **10 Synaptics Chips**.
    *   **Usage**: If no ID is provided, lists all 10 ship systems with their current integrity status.
    *   **System IDs**: `0`: Hyperdrive, `1`: Impulse, `2`: Sensors, `3`: Transp, `4`: Ion Beams, `5`: Torps, `6`: Computer, `7`: Life Support, `8`: Shields, `9`: Aux.
*   **Crew Management**: 
    *   Initial personnel number depends on ship class (e.g., 1012 for Explorer, 50 for Escort).    *   **Vital Integrity**: If **Life Support** drops below 75%, the crew will start suffering periodic losses.
    *   **Shield Integrity**: If the **Shield System (ID 8)** integrity is low, the automatic recharge of the 6 quadrants is slowed down.
    *   **Failure Condition**: If crew reaches **zero**, the mission ends and the ship is considered lost.

### 3. Operational Command Guide & Deep Space Cryptography

The Space GL bridge operates via a high-precision Command Line Interface (CLI). Beyond navigation and combat, the simulator implements a sophisticated **Electronic Warfare** system based on real-world cryptography.

#### 🛰️ Advanced Navigation & Utility Commands
*   `nav <H> <M> <W> [F]`: **Hyperdrive Navigation**. Plots a Hyperdrive course towards relative coordinates. `H`: Heading (0-359), `M`: Mark (-90/+90), `W`: Distance in quadrants, `F`: Optional Hyperdrive Factor (1.0 - 9.9).
*   `imp <H> <M> <S>`: **Impulse Drive**. Sub-light navigation within the current sector. `S`: Speed in percentage (1-100%). Use `imp <S>` to only adjust speed.
*   `jum <Q1> <Q2> <Q3>`: **Wormhole Jump**. Generates a spatial tunnel to a distant quadrant. Requires **5000 Energy and 1 Aetherium Crystal**.
*   `apr <ID> [DIST]`: **Automatic Approach**. Autopilot intercepts the specified object at the desired distance (default 2.0). Works galaxy-wide for ships and comets.
*   `cha`: **Chase Target**. Actively pursues the currently locked (`lock`) target.
*   `rep <ID>`: **Repair System**. Initiates repairs on a subsystem (1: Hyperdrive, 2: Impulse, 3: Sensors, 4: Ion Beams, 5: Torpedoes, etc.).
*   `inv`: **Inventory Report**. Detailed list of resources in cargo (Aetherium, Neo-Titanium, Nebular Gas, etc.).
*   `dam`: **Damage Report**. Detailed status of hull integrity and systems.
*   `cal <Q1> <Q2> <Q3>`: **Hyperdrive Calculator**. Calculates the vector towards the center of a distant quadrant.
*   `cal <Q1> <Q2> <Q3> <X> <Y> <Z>`: **Pinpoint Calculator**. Calculates the vector towards precise sector coordinates `[X, Y, Z]` in a distant quadrant. Provides arrival times and suggests the exact `nav` command to copy.
*   `ical <X> <Y> <Z>`: **Impulse Calculator (ETA)**. Provides a full navigational computation for precise sector coordinates [0.0 - 10.0], including real-time travel time at current power levels.
*   `who`: **Captains Registry**. Lists all commanders currently active in the galaxy, their tracking IDs, and current position. Crucial for identifying allies or potential predators before entering a sector.
*   `sta`: **Status Report**. Complete systems diagnostic, including energy levels, hardware integrity, and power distribution.
*   `hull`: **Composite Reinforcement**. If you have **100 units of Composite** in cargo, this command applies reinforced plating to the hull (+500 HP physical shield), visible as gold in the HUD.

#### 🛡️ Tactical Cryptography: Communication "Frequencies"
In Space GL, encryption is not just about security—it's a **tactical frequency choice**. Each algorithm acts as a separate communication band.

*   **Identity & Signature (Ed25519)**: Every radio packet is digitally signed. If you receive a message with a **`[VERIFIED]`** tag, you have mathematical certainty it comes from the declared captain and has not been altered by enemy sensors or spatial phenomena.
*   **Cryptographic Frequencies (`enc <TYPE>`)**:
    *   **AES (`enc aes`)**: The balanced Alliance Command standard. Secure and optimized for modern hardware.
    *   **PQC (`enc pqc`)**: **Post-Quantum Cryptography (ML-KEM)**. Represents the ultimate defense against Quantum Computers from the Swarm or temporally advanced civilizations. The most secure protocol available.
    *   **ChaCha (`enc chacha`)**: Ultra-fast, ideal for quick communications in unstable Deep Space conditions.
    *   **Camellia (`enc camellia`)**: Standard protocol of the Xylari Empire, known for its elegant structure and resistance to brute-force attacks.
    *   **ARIA (`enc aria`)**: Standard used by the Korthian Alliance for coalition operations.
    *   **IDEA / CAST5**: Protocols often used by resistance groups (Maquis) or mercenaries to avoid standard Alliance Command monitoring.
    *   **OFF (`enc off`)**: Clear communication. Risky, but useful for universal distress calls readable by anyone.

**Tactical Implication**: If an allied fleet decides to operate on "ARIA Frequency", every member must set `enc aria`. Those staying on AES will see only static noise (**`<< SIGNAL DISTURBED >>`**), allowing secure and secret communications even in crowded sectors.

### 📡 Communications and Miscellaneous
*   `rad <MSG>`: Sends radio message to all (Open channel).
    *   **Faction Table (@Fac)**:
        | Faction | Full Name | Abbreviated |
        | :--- | :--- | :--- |
        | **Alleanza** | `@Alliance` | `@Fed` |
        | **Korthian** | `@Korthian` | `@Kli` |
        | **Xylarii** | `@Xylari` | `@Rom` |
        | **Swarm** | `@Swarm` | `@Bor` |
        | **Vesperiani** | `@Vesperian` | `@Car` |
        | **Dominio** | `@Ascendant` | `@Jem` |
        | **Quarzitei** | `@Quarzite` | `@Tho` |
        | **Gilded** | `@Gilded` | `@Fer` |
        | **Specie 8472** | `@FluidicVoid`| `@8472` |
        | **Saurian / Cryos / Apex** | Full Name | - |
*   `rad #ID <MSG>`: Private message to player ID.
*   `psy`: **Psychological Warfare**. Attempts a bluff (Corbomite Maneuver).
*   `axs` / `grd`: Toggles 3D visual guides (Axes / Grid).
*   `h` (hotkey): Hides the HUD for a "cinematic" view.
        | **Alliance** | `@Alliance` | `@Fed` |
        | **Korthian** | `@Korthian` | `@Kli` |
        | **Xylari** | `@Xylari` | `@Rom` |
        | **Swarm** | `@Swarm` | `@Bor` |
        | **Vesperian** | `@Vesperian` | `@Car` |
        | **Ascendant** | `@Ascendant` | `@Jem` |
        | **Quarzite** | `@Quarzite` | `@Tho` |
        | **Gilded** | `@Gilded` | `@Fer` |
        | **Fluidic Void** | `@FluidicVoid`| `@8472` |
        | **Saurian / Cryos / Apex** | Full Name | - |
*   `rad #ID <MSG>`: Private message to player ID.
*   `psy`: **Psychological Warfare**. Attempts bluff (Corbomite Maneuver).
*   `axs` / `grd`: Activates/Deactivates 3D visual guides (Axes / Grid).
The 3D viewer is not a simple graphic window but an extension of the command bridge providing overlaid telemetry data (Augmented Reality) to support the captain's decision-making process.

#### 🎯 Integrated Tactical Projection
The system uses spatial projection algorithms to anchor information directly above detected entities:
*   **Targeting Tags**: Each vessel is identified with an advanced dynamic label.
    *   **Alliance Players**: Displays `Alliance - [Class] ([Captain Name])`.
    *   **Alien Players**: Displays `[Faction] ([Captain Name])`.
    *   **NPC Units**: Displays `[Faction] [ID]`.
*   **Health Bars**: Chromatic health indicators (Green/Yellow/Red) displayed above each ship and station, allowing instant evaluation of enemy status without consulting text logs.
*   **Visual Latching**: Combat effects (Ion Beams, explosions) are temporally synchronized with server logic, providing immediate visual feedback on hit impact.

#### 🧭 3D Tactical Compass (`axs`)
By activating visual axes (`axs`), the simulator projects a spherical reference system centered on your ship.

**Axis Convention Note**: The graphics engine uses the **OpenGL (Y-Up)** standard.
*   **X (Red)**: Transverse axis (Left/Right).
*   **Y (Green)**: Vertical axis (Up/Down). This is the rotation axis for *Heading*.
*   **Z (Blue)**: Longitudinal axis (Depth/Forward motion).
*   **Boundary Coordinates**: At the center of each face of the tactical cube, the coordinates `[X,Y,Z]` of adjacent quadrants are projected.

---

### 🖥️ Server Architecture and GDIS Telemetry

Upon launching `stellar_server`, the system performs a complete diagnostic of the host infrastructure, displaying an **GDIS (Library Computer Access and Retrieval System)** telemetry panel.

#### 📊 Monitored Data
*   **Logical Infrastructure**: Host identification, Linux kernel version, and core libraries (**GNU libc**).
*   **Memory Allocation**: 
    *   **Physical RAM**: Status of available physical memory.
    *   **Shared Segments (SHM)**: Monitoring of active shared memory segments, vital for the *Direct Bridge* IPC.
*   **Network Topology**: List of active interfaces, IP addresses, and real-time traffic statistics (**RX/TX**) retrieved from the kernel.
*   **Deep Space Dynamics**: System load average and number of active logical tasks.

This diagnostic ensures the server operates in a nominal environment before opening Deep Space communication channels on port `3073`.


#### 📟 HUD Telemetry and Monitoring
The on-screen interface (Overlay) provides constant monitoring of vital parameters:
*   **Reactor & Shield Status**: Real-time display of available energy and average defensive grid power.
*   **Cargo Monitoring**: Explicit monitoring of **CARGO ANTIMATTER** and **CARGO TORPEDOES** reserves for rapid resupply.
*   **Hull Integrity**: Physical state of the hull (0-100%). If it drops to zero, the vessel is lost.
*   **Hull Plating**: Golden indicator of Composite-reinforced hull integrity (visible only if present).
*   **Sector Coordinates**: Instant conversion of spatial data into relative coordinates `[S1, S2, S3]` (0.0 - 10.0), mirroring those used in `nav` and `imp` commands.
*   **Threat Detector**: A dynamic counter indicates the number of hostile vessels detected by sensors in the current quadrant.
*   **Deep Space Uplink Diagnostics Suite**: An advanced diagnostic panel (bottom right) monitoring the health of the neural/data link. It shows real-time Link Uptime, **Pulse Jitter**, **Signal Integrity**, protocol efficiency, and the active status of **AES-256-GCM** encryption.

#### 🛠️ View Customization
The commander can configure their interface via quick CLI commands:
*   `grd`: Activates/Deactivates the **Galactic Tactical Grid**, useful for perceiving depth and distances.
*   `axs`: Activates/Deactivates the **AR Tactical Compass**. This holographic compass is anchored to the ship's position: the Azimuth ring (Heading) remains oriented towards Galactic North (fixed coordinates), while the Elevation arc (Mark) rotates with the vessel, providing an immediate navigation reference for combat maneuvers.
*   `h` (hotkey): Completely hides the HUD for a "cinematic" view of the sector.
*   **Zoom & Rotation**: Total control of the tactical camera via mouse or `W/S` keys and directional arrows.

---

## ⚠️ Tactical Report: Threats and Obstacles

### NPC Ship Capabilities
Computer-controlled ships (Korthians, Xylaris, Swarm, etc.) operate with standardized combat protocols:
*   **Primary Armament**: Currently, NPC ships are equipped exclusively with **Ion Beam Banks**.
*   **Firepower**: Enemy Ion Beams inflict constant damage of **10 units** of energy per hit (reduced for balance).
*   **Engagement Range**: Hostile ships will automatically open fire if a player enters within a **6.0 unit** range (Sector).
*   **Fire Rate**: Approximately one shot every 5 seconds.
*   **Tactics**: NPC ships do not use Plasma Torpedoes. Their main strategy consists of direct approach (Chase) or fleeing if energy drops below critical levels.

### ☄️ Plasma Torpedo Dynamics
Torpedoes (`tor` command) are physically simulated weapons with high precision:
*   **Collision**: Torpedoes must physically hit the target (distance **< 0.8**) to explode (increased radius to prevent tunneling).
*   **Guidance**: If launched with an active `lock`, torpedoes correct their course by **35%** per tick towards the target, allowing hits on agile ships.
*   **Comet Chasing**: You can use the `cha` (Chase) command to follow comets along their galactic orbit, making gas collection easier.
*   **Obstacles**: Celestial bodies like **Stars, Planets, and Black Holes** are solid physical objects. A torpedo impacting them will be absorbed/destroyed without hitting the target behind them. Use galactic terrain for cover!
*   **Starbases**: Starbases also block torpedoes. Beware of friendly or incidental fire.

### 🌪️ Space Anomalies and Environmental Hazards
The quadrant is scattered with natural phenomena detectable by both sensors and the **3D tactical view**:
*   **Nebulas (ID 8xxx)**:
    *   **Classes**: Standard, High-Energy, Dark Matter, Ionic, Gravimetric, Temporal.
    *   **Effect**: Clouds of gas and particles that interfere with short and long range sensors (telemetry noise and distortion).
    *   **3D View**: Colored gas volumes based on class (Purple/Blue for Standard, Yellow/Orange for High-Energy, Black/Purple for Dark Matter, etc.).
    *   **Hazard**: Remaining inside (Distance < 2.0) causes constant energy drain and inhibits shield regeneration.
    *   **Advantage**: Provides natural tactical cover (passive cloaking) against enemy sensors.
*   **Pulsars (ID 5xxx)**:
    *   **Effect**: Rapidly rotating neutron stars emitting deadly radiation.
    *   **3D View**: Visible as bright cores with rotating radiation beams.
    *   **Hazard**: Approaching too close (Distance < 2.5) severely damages shields and rapidly kills crew via radiation poisoning.
*   **Comets (ID 6xxx)**:
    *   **Effect**: Fast-moving objects traversing the sector.
    *   **3D View**: Icy nuclei with a blue trail of gas and dust.
    *   **Resource**: Approaching the tail (< 0.6) allows the collection of rare gases.
*   **Asteroid Fields (ID 8xxx)**:
    *   **Effect**: Clusters of space rocks of various sizes.
    *   **3D View**: Rotating brown rocks with irregular shapes.
    *   **Hazard**: Navigating inside at high impulse speed (> 0.1) causes continuous damage to shields and engines.
*   **Derelict Ships (ID 7xxx)**:
    *   **Effect**: Abandoned Alliance Command or alien vessels.
    *   **3D View**: Dark and cold hulls drifting slowly in space.
    *   **Opportunity**: Can be explored via the `bor` (boarding) command to recover Aetherium, Synaptics Chips, or to perform instant emergency repairs.
*   **Minefields (ID 9xxx)**:
    *   **Effect**: Defensive zones with cloaked mines placed by hostile factions.
    *   **3D View**: Small spiked metallic spheres with pulsing red light (visible only at distance < 1.5).
    *   **Hazard**: Detonation causes massive damage to shields and energy. Use the `scan` command to detect them before entering the sector.
*   **Communication Buoys (ID 15xxx)**:
    *   **Effect**: Alliance Command network nodes for sector monitoring.
    *   **3D View**: Lattice structures with rotating antennas and pulsing blue signals.
    *   **Advantage**: Being near a buoy (**Distance < 1.2**) enhances long-range sensors (`lrs`), revealing the exact composition of adjacent quadrants (e.g., `H:1 P:2`) instead of a simple total count.
*   **Defense Platforms (ID 11xxx)**:
    *   **Effect**: Heavily armed static sentinels protecting strategic zones.
    *   **3D View**: Hexagonal metallic structures with active Ion Beam banks and an energy core.
    *   **Hazard**: Extremely dangerous if approached without full shields. Automatically fire at non-cloaked targets within a 5.0 unit range.
*   **Spatial Rifts (ID 12xxx)**:
    *   **Effect**: Unstable natural teleporters caused by tears in the spacetime fabric that ignore normal Hyperdrive navigation laws.
    *   **3D View**: Rendered as rotating cyan energy rings. On the galactic map and sensors (`srs` or `lrs`), they are marked with **Cyan** color or the letter **R**.
    *   **Risk/Opportunity**: Entering a rift (Distance < 0.5) instantly projects the ship to a completely random point in the universe (random quadrant and sector). It can be a fatal hazard (e.g., Swarm territory) or the only quick escape route during a critical attack.
*   **Space Monsters (ID 13xxx)**:
    *   **Crystalline Entity**: Geometric predator that chases ships and fires crystalline resonance beams.
    *   **Space Amoeba**: Giant lifeform that drains energy on contact.
    *   **Hazard**: Extremely rare and dangerous. Require group tactics or maximum firepower.
*   **Ion Storms**:
    *   **Effect**: Random global events synchronized in real-time on the map.
    *   **Frequency**: High (statistical average of one event every 5-6 minutes).
    *   **Technical Impact**: Hitting a storm **instantly halves** sensor health (ID 2).
    *   **Functional Degradation**: Damaged sensors (< 100%) produce "noise" in SRS/LRS reports (ghost objects, missing data, or imprecise coordinates). Below 25%, sensors become nearly unusable.
    *   **Technical Details**: Checked every 1000 ticks (33s), 20% event probability, with a 50% specific weight for ion storms.
    *   **Hazard**: Can blind sensors or violently push the ship off course.

## 📡 Game Dynamics and Events
The universe of Space GL is brought to life by a series of dynamic events that require quick thinking from the command bridge.

#### ⚡ Random Sector Events
*   **Deep Space Surges**: Sudden fluctuations that can partially recharge energy reserves or cause an overload resulting in energy loss.
*   **Spatial Shear**: Violent gravitational currents that hit the ship during navigation, physically pushing it off course.
*   **Life Support Emergency**: If the `Life Support` system is damaged, the crew will start suffering casualties. This is a critical condition requiring urgent repairs or docking at a starbase.

#### 🚨 Tactical and Emergency Protocols
*   **Corbomite Bluff**: The `psy` command allows you to transmit a fake nuclear threat signal. If successful, enemy vessels will immediately enter retreat mode.
*   **Emergency Rescue Protocol**: In case of vessel destruction or fatal collision, upon re-entry Alliance Command Command will initiate an automatic rescue mission, repositioning the ship in a safe sector and restoring core systems to 80% integrity.
*   **Boarding Resistance**: Boarding operations (`bor`) are not without risks; teams can be repelled, causing internal damage to your ship's Synaptics circuits.

---

## 🎖️ Commanders Historical Registry

This section provides an official reference to the most celebrated commanders of the galaxy, useful for player inspiration or designating elite vessels.

### 🔴 Galactic Power Commanders

#### 1. Korthian Empire
<table>
<tr>
    <td><img src="readme_assets/gpc-korthian.png" alt="Korthian Empire" width="200"/></td>
  </tr>
</table>
*   **Kor**: The legendary "Dahar Master", pioneer of early tactical contacts with the Alliance.
*   **Khorak**: Supreme Commander of Korthian forces during the Great Galactic War.
*   **Dahar**: Chancellor and veteran of the Korthian Civil War.

#### 2. Xylari Star Empire
<table>
<tr>
    <td><img src="readme_assets/gpc-xylari.png" alt="Xylari Star Empire" width="200"/></td>
  </tr>
</table>
*   **Valerius**: Commander of D'deridex class vessels and historic tactical adversary.
*   **Alara**: Operational commander and strategist specializing in infiltration operations.
*   **Donatra**: Commander of the *Valdore*, known for tactical cooperation during the Shinzon crisis.

#### 3. Swarm Collective
<table>
<tr>
    <td><img src="readme_assets/gpc-swarm.png" alt="Swarm Collective" width="200"/></td>
  </tr>
</table>
*   🤖 **Node-Alpha 01**: The first hive intelligence to coordinate the technological assimilation of entire star systems.
*   **The Queen**: Central coordination node of the Collective.
*   **Unimatrix 01**: Command designation for Diamond class vessels or Tactical Cubes.

#### 4. Vesperian Union
<table>
<tr>
    <td><img src="readme_assets/gpc-vesperian.png" alt="Korthian Empire" width="200"/></td>
  </tr>
</table>
*   **Gul Dukat**: Leader of occupation forces and commander of station Terok Nor.
*   **Gul Madred**: Expert in interrogation and intelligence operations.
*   **Gul Damar**: Leader of the Vesperian resistance and successor to supreme command.

#### 5. Ascendant (Ascendant)
<table>
<tr>
    <td><img src="readme_assets/gpc-ascendant.png" alt="Ascendant" width="200"/></td>
  </tr>
</table>
*   **Remata'Klan**: First of the Ascendant, symbol of discipline and absolute loyalty.
*   **Ikat'ika**: Commander of ground forces and master of tactical combat.
*   **Karat'Ulan**: Operational commander in the Gamma Quadrant.

#### 6. Quarzite Matrix
<table>
<tr>
    <td><img src="readme_assets/gpc-quarzite-matrix.png" alt="Quarzite Matrix" width="200"/></td>
  </tr>
</table>
*   **Loskene**: Commander known for employing the Quarzite energy web.
*   **Terev**: Ambassador and commander involved in territorial disputes.
*   **Sthross**: Flotilla commander expert in energy confinement tactics.

#### 7. Saurian Legion
<table>
<tr>
    <td><img src="readme_assets/gpc-saurian.png" alt="Saurian Legion" width="200"/></td>
  </tr>
</table>
*   **Slar**: Warrior commander active during early expansion phases.
*   **S'Sless**: Captain in charge of frontier outpost defense.
*   **Varn**: Fleet commander during skirmishes in the Alpha Quadrant.

#### 8. Gilded Cartel
<table>
<tr>
    <td><img src="readme_assets/gpc-guilded.png" alt="Gilded Cartel" width="200"/></td>
  </tr>
</table>
*   **DaiMon Bok**: Known for employing simulation technologies and personal vendettas.
*   **DaiMon Tog**: Commander specializing in forced technology acquisitions.
*   **DaiMon Goss**: Tactical representative during negotiations for Wormhole control.

#### 9. Fluidic Void
<table>
<tr>
    <td><img src="readme_assets/gpc-fluidic.png" alt="Fluidic Void" width="200"/></td>
  </tr>
</table>
*   **Boothby (Impersonator)**: Entity dedicated to infiltration and study of Fleet command.
*   **Bio-Ship Alpha**: Designation of the tactical coordinator of organic vessels.
*   **Valerie Archer (Impersonator)**: Infiltration subject for deep reconnaissance missions.

#### 10. Cryos Enclave
<table>
<tr>
    <td><img src="readme_assets/gpc-cryos.png" alt="Cryos Enclave" width="200"/></td>
  </tr>
</table>
*   **Thot Pran**: High-ranking commander during the offensive in the Alpha Quadrant.
*   **Archon**: Operational leader during the strategic alliance with the Ascendant.
*   **Thot Tarek**: Commander of Cryos strike forces.

#### 11. Apex
<table>
<tr>
    <td><img src="readme_assets/gpc-apex.png" alt="Apex" width="200"/></td>
  </tr>
</table>
*   **Karr**: Alpha Apex expert in large-scale hunt simulations.
*   **Idrin**: Veteran hunter and commander of prey vessels.
*   **Turanj**: Commander specializing in long-range tracking.

---

## 🎖️ Historical Register of Commanders (GDIS Database)

The GDIS central database preserves the deeds of commanders who shaped the boundaries of known space through darkness and light.

## 🌌 Alleanza Stellare (L'Alleanza)

<table>
<tr>
    <td><img src="readme_assets/com-alliance3.png" alt="Alliance Emblem" width="400"/></td>
  </tr>
</table>

---

> **Command Note:** The Stellar Alliance is not merely a military coalition, but an ideal of order and progress standing against the chaos of the frontier territories and the darkness of unexplored quadrants.

---

### 🏛️ Overview
The **Stellar Alliance** stands as the primary bastion of stability and cooperation among the powers of the quadrant. Founded on the principles of **proactive diplomacy**, **scientific exploration**, and **collective defense**, the Alliance serves as the coordinating entity between diverse civilizations to counter systemic threats that endanger known space.

### 🛡️ Strategic Pillars

* **Strategic Doctrine** Unlike expansionist or collectivist powers, the Alliance favors an approach based on **multilateralism**. Its strength lies in its ability to integrate heterogeneous tactics and technologies from various cultures under a unified command.

* **Diplomatic Excellence** It is the hub of galactic negotiation, renowned for its skill in transforming century-long conflicts into lasting peace treaties through dialogue and tactical mediation.

* **Military Capability** Although peace-oriented, the Alliance maintains a highly specialized elite fleet. It excels in:
    * Defense of strategic chokepoints.
    * Mapping of complex spatial anomalies.
    * Management of large-scale humanitarian or biological crises.

* **Operational Objectives** The preservation of freedom of navigation, the protection of interstellar trade, and organized resistance against forces of technological assimilation or biological annihilation.
---

<table>
<tr>
    <td><img src="readme_assets/com-niklaus.png" alt="Emblem" width="200"/></td>
    <td><img src="readme_assets/actor-HighAdmiralHyperionNiklaus.png" alt="High Admiral Hyperion Niklaus" width="200"/></td>
  </tr>
</table>

*   🛡️ **High Admiral Hyperion Niklaus**: Known as "The Wall of Orion," he led the defense of the Aegis during the first great Swarm invasion.

<table>
<tr>
    <td><img src="readme_assets/com-LyraVance.png" alt="Emblem" width="200"/></td>
    <td><img src="readme_assets/actor-CaptainLyraVance.png" alt="Captain Lyra Vance" width="200"/></td>
  </tr>
</table>

*   ⚓ **Captain Lyra Vance**: The legendary explorer who mapped the Einstein-Rosen Bridge to the Delta Quadrant using a Scout-class vessel.

<table>
<tr>
    <td><img src="readme_assets/com-LeandrosThorne.png" alt="Emblem" width="200"/></td>
    <td><img src="readme_assets/actor-CommanderLeandrosThorne.png" alt="Commander Leandros Thorne" width="200"/></td>
  </tr>
</table>

*   📜 **Commander Leandros Thorne**: A refined diplomat and tactician, famous for the Aetherium Treaty that ended the century-long war with the Korthians.

#### ⚔️ 2. Korthian Empire
*   🩸 **Warlord Khorak**: The most brutal tactician of the empire, famous for his "Perpetual Fire" doctrine and the conquest of the Black Sector.
*   🗡️ **General Valkar**: A legendary commander who unified the warring houses under a single banner of galactic conquest.

#### 🎭 3. Xylari Star Empire
*   🐍 **Grand Praetor Nyx**: Master of stealth and sabotage, he vanished for ten years before re-emerging with a ghost fleet in the heart of enemy territory.
*   👁️ **Inquisitor Malakor**: The first to use Camellia encryption frequencies to manipulate enemy sensor data streams.

#### 🕸️ 4. Swarm Collective
*   🤖 **Node-Alpha 01**: The first hive intelligence to coordinate the technological assimilation of entire star systems.
*   🔗 **Unity Prime**: A biomechanical entity tasked with optimizing stellar mass consumption in energy nebulae.

#### 🏛️ 5. Vesperian Union
*   📐 **Legate Thrax**: Architect of galactic defense, known for transforming simple asteroids into impregnable fortresses.

#### 🔮 6. The Ascendancy
*   🛐 **First Archon Voth**: The spiritual and military guide who led his "Ascendant" fleet through the Great Void.

#### 💎 7. Quarzite Matrix
*   💠 **Refraction Zero**: A pure crystalline entity capable of calculating hyperspace routes at a speed exceeding any biological computer.

#### 💰 8. Gilded Cartel
*   📈 **Barone Silas**: The tycoon who monopolized the trade of Void-Essence and Aetherium across three quadrants.

#### ❄️ 9. Enclave Cryos
*   🧊 **Warden Boreas**: Governor of the frozen wastes, expert in thermal guerrilla tactics and signal suppression.

#### 🎯 10. Apex Stalkers
*   🏹 **Alpha Hunter Kael**: Known as "The Ghost of Sector Zero," renowned for never missing a target with his manual-guided plasma torpedoes.

---


### 🔵 Operational Profiles by Vessel Class (Alliance)

In Space GL, the choice of vessel class defines the Commander's operational profile. Below are references for the main classes:

#### 🏛️ Legacy Class (Heavy Cruiser)
The symbol of Alliance exploration. A balanced, versatile, and robust vessel.
*   **Reference Commander**: **Hyperion Niklaus**. His leadership on the original Aegis defined the tactical standards of the academy.

#### 🛡️ Explorer Class (Flagship)
Designed for long-duration missions and first contact. Features the most advanced GDIS systems.
*   **Reference Commander**: **Lyra Vance**. She excelled in using long-range sensors to avoid unnecessary conflicts.

#### ⚔️ Flagship Class (Tactical Cruiser)
The ultimate expression of Alliance firepower, equipped with heavy Ion Beam banks.
*   **Reference Commander**: **Leandros Thorne**. Famous for the coordinated use of localized shields and plasma torpedo volleys.

#### 🔭 Science Vessel (Scientific Explorer)
A vessel specialized in analyzing spatial anomalies and gathering Aetherium.
*   **Reference Commander**: **Inquisitor Malakor** (Acquired). Although Xylari, his theories on spatial resonance are studied in every scientific mission.

#### 🛠️ Other Operational Classes
The Alliance also employs specialized vessels like the **Carrier** class (drone coordination) and **Tactical Cruiser** (perimeter defense), each optimized for specific crisis scenarios.

---

### 🛰️ Alliance Naval Nomenclature (GDIS Standard)

To facilitate tactical coordination, the GDIS system adopts a standardized nomenclature for Alliance vessel components, illustrated here on the "Monoblock" configuration of the Legacy class:

1.  **Primary Hull (Command Module)**: The primary discoidal body, housing command bridges, quarters, and scientific laboratories.
2.  **Tactical Hub (Bridge Module)**: The reinforced upper dome, the computational center for weapon targeting and fleet management.
3.  **Engineering Section (Secondary Hull)**: The integrated rear oblong section, designed to house the plasma reactor and Aetherium tanks.
4.  **Main Deflector Array (Rear Sphere)**: A resonant sphere spaced from the main body, used for particle deflection and Hyperdrive flow stabilization.
5.  **Energy Resonance Rings**: A series of **3 rotating magnetic induction rings** around the tail deflector, responsible for FTL field coherence.
6.  **Structural Pylons (Support Arms)**: Tapered ellipsoid-shaped support arms that rigidly connect the engineering section to the propulsion units.
7.  **Hyperdrive Nacelles (FTL Units)**: Lateral twin nacelles, primary Hyperdrive bubble generators required for superluminal spaceflight.

This elongated architecture, devoid of thin connections ("Neck"), represents the Alliance's technological evolution toward more robust vessels resistant to kinetic impacts.

---

## 💾 Persistence and Continuity
The architecture of Space GL is designed to support a persistent and dynamic galaxy. Every action, from discovering a new planetary system to loading the Cargo Bay, is preserved via a low-level binary archiving system.

#### 🗄️ The Galactic Database (`galaxy.dat`)
The \`galaxy.dat\` file constitutes the historical memory of the simulator. It uses a **Direct Serialization** structure of the server's RAM:
*   **Galaxy Master Matrix**: A 10x10x10 three-dimensional grid storing the mass density and composition of each quadrant (BPNBS encoding).
*   **Entity Registers**: A complete dump of global arrays (\`npcs\`, \`stars_data\`, \`planets\`, \`bases\`), preserving relative coordinates, energy levels, and cooldown timers.
*   **Data Integrity**: Implements a rigid version control (\`GALAXY_VERSION\`). If the server detects a file generated with different structural parameters, it invalidates the loading to prevent memory corruption, regenerating a coherent universe.

#### 🔄 Synchronization Pipeline (Auto-Save)
Continuity is guaranteed by an asynchronous synchronization loop:
*   **Periodic Flush**: Every 60 seconds, the logic thread initiates a save procedure.
*   **Thread Safety**: During the disk I/O operation, the system acquires the `game_mutex`. This ensures that the saved database is an **atomic snapshot** and coherent representation of the entire universe at that precise moment.

#### 🆔 Identity and Profile Restoration
The continuity system for players is based on **Persistent Identity**:
*   **Recognition**: By entering the same captain name used previously, the server queries the database of active and historical players.
*   **Session Recovery**: Global coordinates, strategic inventory, and system states are instantly restored.

#### 🆘 EMERGENCY RESCUE Protocol
In case of vessel destruction, at the next login, Alliance Command activates a recovery protocol: 80% system restoration, emergency resupply, and relocation to a safe sector.

This architecture guarantees that Space GL is not just a game session, but a true evolving space career.

## 🔐 Deep Space Cryptography: Tactical Deep Dive

Space GL implements a multi-layered cryptographic suite that transforms communication security into a true tactical gameplay mechanic. Each algorithm represents a different operational "frequency."

### 📡 Transmission & Authentication Protocols

In addition to algorithm selection, the GDIS system uses advanced protocols to ensure every order originates from the legitimate commander:

*   **Initial Handshake (XOR Obfuscation)**: Upon connection, the client and server negotiate a unique 256-bit **Session Key**. This exchange occurs via an XOR obfuscation protocol based on the sector's **Master Key** (`SPACEGL_KEY`), ensuring no packet is readable without initial authorization.
*   **Ed25519 Digital Signatures**: Every packet sent via radio (`rad`) is digitally signed. The receiver instantly verifies authenticity using elliptic curves. Authentic messages are marked with **`[VERIFIED]`** in green.
*   **Rotating Frequency Integration**: The Initialization Vector (IV) of each message is dynamically modified based on the server's `frame_id`. This makes the system immune to *Replay Attacks*: a message recorded one second ago will be unreadable the next.

### ⚛️ Algorithm Suite (Operational Frequencies)

The `enc <ALGO>` command allows tuning onboard systems to one of the following standards:

#### 1. ML-KEM-1024 (Kyber) - `enc pqc`
*   **Description**: Lattice-based Post-Quantum Cryptography.
*   **Tactical Use**: The pinnacle of galactic secrecy. Used by the **Shadow Section** for communications that must remain protected even against future attacks from quantum computers. Invulnerable to conventional technology.

#### 2. AES-256-GCM - `enc aes`
*   **Description**: Advanced Encryption Standard with Galois/Counter Mode.
*   **Tactical Use**: The official standard of **Alliance Command**. It offers the best balance between extreme security and speed, thanks to the hardware acceleration of Synaptics processors. Includes message authentication (AEAD).

#### 3. ChaCha20-Poly1305 - `enc chacha`
*   **Description**: Modern stream cipher paired with a message authenticator.
*   **Tactical Use**: Preferred by **Scout** and **Escort** class vessels. Extremely fast in environments where computing power is limited, ensuring minimal latency in tactical links.

#### 4. ARIA-256-GCM - `enc aria`
*   **Description**: Certified South Korean block encryption standard.
*   **Tactical Use**: Represents the coalition frequency between the **Alliance** and the **Korthian Empire**. Used for large-scale joint operations.

#### 5. Camellia-256-CTR - `enc camellia`
*   **Description**: High-efficiency block cipher of Earth origin (Japanese).
*   **Tactical Use**: The imperial standard of the **Xylari Star Empire**. Known for its mathematical elegance and resistance to brute-force decryption attempts by infiltrators.

#### 6. IDEA-CBC - `enc idea`
*   **Description**: International Data Encryption Algorithm.
*   **Tactical Use**: The frequency of the **Resistance** and independent groups. Resilient and difficult to analyze for the centralized computers of the great powers.

#### 7. Blowfish-CBC - `enc bf`
*   **Description**: Designed to be fast and compact.
*   **Tactical Use**: Standard commercial protocol of the **Gilded Cartel**. Used to protect Aetherium transactions and cargo manifests.

#### 8. CAST5-CBC - `enc cast`
*   **Description**: Variable-key algorithm (up to 128 bit).
*   **Tactical Use**: Used in border regions for civilian and local government communications.

#### 9. Triple DES (3DES) - `enc 3des`
*   **Description**: Triple application of the Data Encryption Standard.
*   **Tactical Use**: "Legacy" frequency. Used to access historical archives and communicate with ancient automated space stations.

#### 10. SEED-CBC - `enc seed`
*   **Description**: 128-bit block cipher.
*   **Tactical Use**: Primarily used in the heavy industrial and logistical protocols of the Vesperian Union.

#### 11. RC4 Stream - `enc rc4`
*   **Description**: Historic stream cipher.
*   **Tactical Use**: Although considered insecure for state secrets, it is used for raw telemetry links at very low latency where speed is the only priority.

#### 12. DES-CBC - `enc des`
*   **Description**: The original 1970s Earth standard.
*   **Tactical Use**: Mapped to **pre-Hyperdrive signals**. Necessary to decrypt communications from ancient sleeper probes or signals from civilizations in early technological stages.

---

## 🛠️ Technical Specifications & Quick Start

### ⌨️ 3D Viewer Keyboard Controls
Interaction with the `spacegl_3dview` is handled via the following direct inputs:
*   **Arrow Keys**: Rotate camera (Pitch / Yaw).
*   **W / S Keys**: Precise Zoom In / Zoom Out.
*   **H Key**: Toggle HUD (Hide/Show tactical overlay).
*   **ESC Key**: Safely close the 3D Viewer.

### 🚢 Visual ship class aesthetics
Each vessel class features unique 3D design elements:
*   **Explorer Class**: Features a high-detail command saucer with rotating multi-spectral sensor probes.
*   **Heavy Cruiser**: Rugged design with a massive secondary hull and high-intensity cyan deflector dish.
*   **Cloaked Ships**: When cloaking is active, the ship is replaced by a **Blue Glowing Wireframe** effect, visually representing the bending of light around the hull.

### 🔒 Security & Data Integrity
Space GL implements enterprise-grade security for galactic state synchronization:
*   **HMAC-SHA256 Signatures**: Galaxy data files (`galaxy.dat`) and network updates are signed to ensure zero-tampering during transit or storage.
*   **Cryptographic HUD**: Real-time visualization of encryption flags, signature status, and active protocol parameters directly in the tactical interface.

### ⚙️ System Requirements & Dependencies
To compile and run the StarTrek Ultra suite, ensure the following libraries are installed:
*   **FreeGLUT / OpenGL**: Core rendering engine and window management.
*   **GLEW**: OpenGL Extension Wrangler for advanced shader support.
*   **OpenSSL**: Required for the complete cryptographic suite (AES, HMAC, etc.).
*   **POSIX Threads & RT**: Managed via `lpthread` and `lrt` for shared memory and synchronization.

### ⚡ Zero-Latency IPC Architecture
The extreme responsiveness of the system is achieved through a **Zero-Copy Shared Memory** (`/dev/shm`) architecture. The binary client and the 3D engine communicate at RAM speeds, ensuring that every command issued in the console results in an instantaneous visual reaction without network-induced lag on the local machine.

---
*SPACE GL - 3D LOGIC ENGINE. Developed with technical excellence by Nicola Taibi. "Per Tenebras, Lumen"*

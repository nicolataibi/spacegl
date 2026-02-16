# Copyright (C) 2026 Nicola Taibi
%global rel 13
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
* Mon Feb 16 2026 Nicola Taibi <nicola.taibi.1967@gmail.com> - 2026.02.09-%{rel}
Documentation & System Architecture Evolution
## [v2.0.0] - Technical Optimization & Tactical Realism Update
### Summary
-This update marks a significant shift from the legacy architecture (documented in `README_old.md`) to a high-performance, mathematically rigorous framework. The primary focus is on Network Efficiency (v2.0), Dynamic Persistence, and Systemic Combat Physics.
---
### 1. Network & Infrastructure
-Differential Engine Integration: Migrated from a full-state update model to Delta Compression. The server now utilizes bitmasks to transmit only modified data blocks (Transform, Vitals, etc.).
-Bandwidth Optimization: Implementation of the new binary protocol has resulted in a 90-95% reduction in bandwidth consumption compared to the previous SDB/SHM model.
### 2. World Persistence & Dynamic Environment
-Dynamic Wreckage System: NPC destruction now triggers the real-time generation of permanent wrecks within the sector, supplementing existing static derelicts.
-Visual Fidelity: Integrated a dedicated "Dead Hull" shader for all post-combat wreckage to simulate "cold" and scorched materials, improving visual clarity and immersion.
### 3. Physics & Mathematical Balancing
-Quadratic Power Scaling: Navigation physics now follow , where Hyperdrive energy consumption scales quadratically with velocity.
-System Integrity: Added a "Penalties" layer where damaged subsystems directly impact energy efficiency and consumption rates.
-Precision Combat: Torpedo damage is no longer static; a 1.2x multiplier is now applied to direct precision hits.
### 4. Faction-Specific Mechanics & AI
-Material-Based Resistances: Introduced faction-specific hull properties:
-Swarm (Bio-armor): Native damage reduction.
-Gilded (Fragile): Increased damage vulnerability.
-Systemic AI Debuffs: Attacks targeting NPC engines now result in permanent maneuverability degradation during the encounter.
### 5. Developer Technical Deep-Dives
-Interest Management: Added documentation on quadrant-based spatial partitioning.
-Serialization: Detailed implementation guides for bitmask serialization.
-OpenGL State Management: New guidelines for shader state handling to eliminate visual interference between ship hulls and particle effects.


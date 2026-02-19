# Copyright (C) 2026 Nicola Taibi
%global rel 16
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
* Fri Feb 20 2026 Nicola Taibi <nicola.taibi.1967@gmail.com> - 2026.02.09-%{rel}
- F7 - Anisotropic Filtering: Improves the sharpness of slanted surfaces (cycles through 1x, 2x, 4x, 8x, 16x).
- F8 - Starfield Density: Changes the number of background stars (from 1,000 to 8,000). Useful for making space feel denser or reducing the load on the GPU.
- Multi-Tube Independent Logic: The system now supports firing up to four torpedoes in rapid succession.
- Cyclic Reload HUD: The [L] (Loading) status is now correctly displayed for each individual tube.
- Boundary Enforcement: Torpedoes now explode immediately upon reaching quadrant boundaries.
- Universal Visibility: Torpedoes from all players are now rendered in everyone's 3D view (Object Type 28).
- Optimized HUD: Personal HUD indicators accurately track your own torpedoes, while the 3D view displays all tactical ordnance without cluttering the screen with unnecessary labels.
- Quadrants: $40 \times 40 \times 40$ (64,000 quadrants).Sectors (Units): $40 \times 40 \times 40$ per quadrant.Absolute Coordinate System: Migrated from $0 - 400$ to $0.0 - 1600.0$.
- Vastness: The galaxy is now 64 times larger in terms of absolute unit volume, ensuring superior fluidity during high-speed movement.
- All navigation commands (nav, imp, apr) and calculators (cal, ical) now operate with hundredth-degree precision (%.2f).
- Short-Range Sensors (srs) and status reports (sta) now display coordinates and distances with two decimal places.
- Hyperdrive Recalibration: Warp Factor 9.9 traverses the galaxy's diagonal ($\approx 2771$ units) in 40 real-world seconds.
- HUD ETA: Added a yellow field in the 3D viewer that displays the Estimated Time of Arrival in seconds; this is only visible when a destination is set.
- Tactical Cube: The quadrant frame has been scaled to $40 \times 40 \times 40$.Tactical Grid: Now covers the entire sector volume with reference lines every 10 units.
- Precision has been increased to "millimeter" level, with a stopping tolerance of 0.01 units (ten times more precise). This is critical for docking and boarding maneuvers.
- Full HD (**1920x1080**) support via `TACTICAL_CUBE_W/H` macros.

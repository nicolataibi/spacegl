# Copyright (C) 2026 Nicola Taibi
%global rel 17
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
* Sun Feb 22 2026 Nicola Taibi <nicola.taibi.1967@gmail.com> - 2026.02.09-%{rel}
The conceptual differences between README.md (the current v2.8 version) and README-old.md (referring to previous versions, approx. v2.3/v2.4) reflect the project's evolution from a
  basic space simulator to a high-performance, high-fidelity tactical engine.

  Here are the primary conceptual differences:


  1. Astrometric Scale and Resolution
   * README-old.md: Focuses on the introduction of the 1600x galactic scale and hundredth-unit precision. The 40x40x40 quadrant system was presented as a new feature.
   * README.md: The 40x40x40 scale is now the established standard. It introduces 64-bit grid saturation, utilizing an 18-digit encoding (BPNBS) to map complex objects like Quasars
     without increasing network traffic overhead.


  2. Complexity of Celestial Entities
   * README-old.md: Manages standard entities (Stars, Planets, Black Holes, Pulsars).
   * README.md: Introduces Quasars (Type 29) as physically interactive and orbitable objects with 7 distinct scientific classifications. It also adds 3D spatial awareness to the text
     interface through chromatic depth coding (Green/Yellow/Red) in the lrs command.


  3. Performance Architecture (SDB Model)
   * README-old.md: Describes a basic Client-Server architecture using Shared Memory to reduce local latency.
   * README.md: Evolves into the Deep Space-Direct Bridge (SDB) model with "Pro-Performance" optimizations:
       * 64-byte Cache-Line Alignment: Maximizes throughput on multi-core CPUs and eliminates False Sharing.
       * Zero-Loss FX v2: Guarantees that every tactical effect (explosions, impacts) is rendered synchronously across all clients.
       * Independent Torpedo Entities: Projectiles are now autonomous galactic entities integrated into spatial partitioning, enabling large-scale battles without server-side lag.


  4. Visualization and Rendering
   * README-old.md: Utilizes immediate mode rendering techniques.
   * README.md: Implements Vertex Buffer Objects (VBO) for the tactical grid and starfield, drastically reducing CPU draw calls. It introduces advanced programmable shaders (e.g.,
     Magenta Pulsing for Quasars, Dead Hull shader for wrecks).


  5. Security and Data Integrity
   * README-old.md: Features a security handshake based on XOR obfuscation and session keys.
   * README.md: Implements a full military-grade cryptographic suite, including HMAC-SHA256 Signatures for every packet and support for Post-Quantum Cryptography (ML-KEM). The "Tactical
     Frequency" system (different algorithms creating isolated channels) is much more detailed.


  6. Survival and RPG Mechanics
   * README-old.md: Basic management of energy and shields.
   * README.md: Introduces a critical Life Support system linked to crew count, the Renegade protocol for friendly fire, and advanced cargo management with specific resources (Graphene,
     Synaptics, Composite) required for field repairs and structural reinforcement.


  In summary, while the "old" README documents the foundations of navigation and networking, the "new" README documents a mature system optimized for 60z mass combat and ultra-dense
  astrometric simulation.

# Copyright (C) 2026 Nicola Taibi
%global rel 20
Name:           spacegl
Version:        2026.02.09
Release:        %{rel}%{?dist}
Summary:        Space GL: A space exploration & combat game, Multi-User Client-Server Edition
License:        GPL-3.0-or-later
URL:            https://github.com/nicolataibi/spacegl
Source0:        https://github.com/nicolataibi/spacegl/archive/refs/tags/%{version}-%{rel}.tar.gz

BuildRequires:  gcc
BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  freeglut-devel
BuildRequires:  mesa-libGLU-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  glew-devel
BuildRequires:  openssl-devel
BuildRequires:  desktop-file-utils
BuildRequires:  ncurses-devel
BuildRequires:  glfw-devel
BuildRequires:  vulkan-loader-devel
BuildRequires:  glslc

Requires:       freeglut
Requires:       mesa-libGLU
Requires:       mesa-libGL
Requires:       glew
Requires:       openssl
Requires:       glfw
Requires:       vulkan-loader
Requires:       %{name}-data = %{version}-%{release}

%description
Space GL is a high-performance 3D multi-user client-server game engine.
It features real-time galaxy synchronization via shared memory (SHM),
advanced cryptographic communication frequencies (AES, PQC, etc.),
and a technical 3D visualizer based on OpenGL and FreeGLUT.

%package data
Summary: Data files for %{name}
BuildArch: noarch
Requires: %{name} = %{version}-%{release}

%description data
Data files (graphics, sounds, shaders, and images) for Space GL.

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
mkdir -p %{buildroot}%{_datadir}/%{name}/readme_assets
mkdir -p %{buildroot}%{_datadir}/%{name}/shaders

# Install compiled shaders
install -p -m 0644 build/shaders/*.spv %{buildroot}%{_datadir}/%{name}/shaders/

# Install assets
cp -p readme_assets/*.jpg %{buildroot}%{_datadir}/%{name}/readme_assets/
cp -p readme_assets/*.png %{buildroot}%{_datadir}/%{name}/readme_assets/


# Install binaries
install -p -m 0755 spacegl_server %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_client %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_3dview %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_viewer %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_vulkan %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_hud %{buildroot}%{_bindir}/


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
%{_bindir}/spacegl_vulkan
%{_bindir}/spacegl_hud
%{_bindir}/%{name}-server
%{_bindir}/%{name}-client
%{_datadir}/applications/%{name}.desktop

%files data
%dir %{_datadir}/%{name}/
%{_datadir}/%{name}/readme_assets/
%{_datadir}/%{name}/shaders/

%changelog
* Thu Mar 5 2026 Nicola Taibi <nicola.taibi.1967@gmail.com> - 2026.02.09-%{rel}
1. HUD Data Correction (spacegl_3dview)
  We identified and resolved a data swap in the textual HUD. Previously, the values for the Left (L) and Right (RI) shields were inverted. I swapped the internal indices so that Index 5 now correctly represents the Left sector and Index 4 represents the Right sector, ensuring the telemetry matches the ship's physical state.


2. Shield Pitch Alignment (The "Mark" Issue)
   We fixed the visual positioning of the shield sectors during vertical maneuvers:
   * The Problem: While the ship tilted up or down (Mark/Pitch), the shield sectors remained static or rotated on the wrong       axis, causing a visual detachment from the hull.
   * The Solution: I updated the transformation logic in drawShieldEffect to calculate a dynamic pitch axis based on the ship's current heading. 
   * Final Refinement: To ensure perfect accuracy in spacegl_3dview, I synchronized its rotation sequence with the ship's actual 3D model transformation (Heading - 90° followed by Mark).

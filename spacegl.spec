# Copyright (C) 2026 Nicola Taibi
%global rel 19
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
BuildRequires:  ncurses-devel


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
* Thu Mar 5 2026 Nicola Taibi <nicola.taibi.1967@gmail.com> - 2026.02.09-%{rel}
   1. Shield Visual Alignment (spacegl_3dview):
      Corrected the horizontal rotation of the shield effect. The previous 90-degree offset was removed, ensuring the shield sectors now align perfectly with the ship's heading.

   2. HUD Refinement (spacegl_3dview):
      Updated the shield HUD labels to eliminate ambiguity (using F, RE, T, B, L, RI). We also corrected the SHIELDS AVG calculation by dividing the total value by 100 to display an accurate percentage.

   3. HUD Logic Fix (spacegl_hud):
      Resolved a data swap in the ncurses HUD where the Left and Right shield values were inverted.

   4. Dismantle Effect Repair (spacegl_vulkan):
      Fixed the dis (dismantle) command visual effect. The previous scale was too large, causing the camera to be "inside" the effect and triggering back-face culling. We reduced the scale to a realistic range (1x-3x) and stabilized the event loop logic, also adding support for the resource recovery effect (IPC_EV_RECOVERY).

   5. Ionic Beam Stability (spacegl_vulkan):
      Fixed the intermittent disappearance of the ion beams. We added safety checks for vector normalization to prevent NaN (Not-a-Number) results, which previously caused the beam to vanish during vertical shots or when firing at very close targets.

   6. Build System Optimization (Makefile):
      Optimized the build process by fixing the spacegl_vulkan target. It no longer relinks unnecessarily on every make execution, as it now correctly depends on the physical shader files instead of a virtual .PHONY target.

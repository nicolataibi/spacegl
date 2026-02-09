%global rel 1
Name:           spacegl
Version:        2026.02.09
Release:        %{rel}%{?dist}
Summary:        Space GL: A space exploration & combat game, Multi-User Client-Server Edition
License:        GPL-3.0-or-later
URL:            https://github.com/nicolataibi/spacegl
Source0:        https://github.com/nicolataibi/spacegl/archive/refs/tags/%{name}-%{version}-%{rel}.tar.gz

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
Requires: %{name}%{?_isa} = %{version}-%{release}
Summary: Data files for Space GL
BuildArch: noarch

%description data
Data files (graphics, sounds, and images) for Space GL.

%prep
%setup -q -n %{name}-%{version}-1

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
Exec=env SPACEGL_KEY="thisisasecretkeyforencryption" spacegl-client
Icon=applications-games
Terminal=true
Type=Application
Categories=Game;Simulation;
EOF

desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop

%files
%license LICENSE.txt
%doc README_it.md README.md 

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
* Mon Feb 09 2026 Nicola Taibi <nicola.taibi.1967@gmail.com> - 2026.02.09-1
- Renamed project to Space GL

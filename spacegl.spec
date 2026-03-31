# License: GPL-3.0-or-later
Name:           spacegl
Version:        2026.03.31.01
Release:        %autorelease
Summary:        Space GL: A space exploration & combat game, Multi-User Client-Server Edition
License:        GPL-3.0-or-later
URL:            https://github.com/nicolataibi/spacegl
Source0:        %{url}/archive/refs/tags/%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  freeglut-devel
BuildRequires:  mesa-libGLU-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  glew-devel
BuildRequires:  openssl-devel
BuildRequires:  ncurses-devel
BuildRequires:  glfw-devel
BuildRequires:  vulkan-loader-devel
BuildRequires:  glslc

# Removed explicit library Requires as they are automatically handled by RPM
# Kept only the mandatory link to the data subpackage
Requires:       %{name}-data = %{version}-%{release}

%description
Space GL is a high-performance 3D multi-user client-server game engine.
It features real-time galaxy synchronization via shared memory (SHM),
advanced cryptographic communication frequencies,
and a technical 3D visualizer based on OpenGL and FreeGLUT. [cite: 3]

%package data
Summary: Data files for %{name}
BuildArch: noarch
# Requires main package for consistency
Requires: %{name} = %{version}-%{release}

%description data
Data files (graphics, sounds, shaders, and images) for Space GL. [cite: 4]

%prep
# Setup macro adjusted for standard naming
%setup -q

%build
# Force Fedora build flags 
%set_build_flags
# Build the project (removed 'make clean' as requested by reviewer) 
%make_build

%check
# Run internal test suite 
%make_build check

%install
# Creating directory structure
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/%{name}/readme_assets
mkdir -p %{buildroot}%{_datadir}/%{name}/shaders

# Install compiled shaders with proper permissions
install -p -m 0644 build/shaders/*.spv %{buildroot}%{_datadir}/%{name}/shaders/

# Install assets using 'install' instead of 'cp' as requested 
install -p -m 0644 readme_assets/*.jpg %{buildroot}%{_datadir}/%{name}/readme_assets/
install -p -m 0644 readme_assets/*.png %{buildroot}%{_datadir}/%{name}/readme_assets/

# Install binaries
install -p -m 0755 spacegl_server %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_client %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_3dview %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_viewer %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_vulkan %{buildroot}%{_bindir}/
install -p -m 0755 spacegl_hud %{buildroot}%{_bindir}/

# Install helper scripts as user commands
install -p -m 0755 spacegl_server.sh %{buildroot}%{_bindir}
install -p -m 0755 spacegl_client.sh %{buildroot}%{_bindir}

# Note: .desktop file removed as this is a command-line client/server application

%files
%license LICENSE.txt
%doc README_it.md README.md HOWTO.txt
%{_bindir}/spacegl_server
%{_bindir}/spacegl_client
%{_bindir}/spacegl_3dview
%{_bindir}/spacegl_viewer
%{_bindir}/spacegl_vulkan
%{_bindir}/spacegl_hud
%{_bindir}/spacegl_server.sh
%{_bindir}/spacegl_client.sh

%files data
%dir %{_datadir}/%{name}/
%{_datadir}/%{name}/readme_assets/
%{_datadir}/%{name}/shaders/

%changelog
%autochangelog

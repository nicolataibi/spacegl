# License: GPL-3.0-or-later
Name:           spacegl
Version:        2026.04.16.03
Release:        %autorelease

Summary:        Space GL: A space exploration & combat game, Multi-User Client-Server Edition

# Disable debuginfo package generation and binary stripping
# This ensures diagnostic tools like spacegl_diag keep their symbols
%global debug_package %{nil}
%global __strip /bin/true

License:        GPL-3.0-or-later
URL:            https://github.com/nicolataibi/spacegl
Source0:        %{url}/archive/refs/tags/%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  cmake
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
BuildRequires:  help2man

# Removed explicit library Requires as they are automatically handled by RPM
# Kept only the mandatory link to the data subpackage
Requires:       %{name}-data = %{version}-%{release}

%description
Space GL is a high-performance 3D multi-user client-server game engine.
It features real-time galaxy synchronization via shared memory (SHM),
advanced cryptographic communication frequencies,
and a technical 3D visualizer based on OpenGL and FreeGLUT.

%package data
Summary: Data files for %{name}
BuildArch: noarch
# Requires main package for consistency
Requires: %{name} = %{version}-%{release}

%description data
Data files (graphics, sounds, shaders, and images) for Space GL.

%prep
# Setup macro adjusted for standard naming
%setup -q

%build
# Build the project using explicit cmake commands to ensure compatibility with Mock/Copr
cmake -S . -B build \
      -DCMAKE_INSTALL_PREFIX=%{_prefix} \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build %{?_smp_mflags}

%check
# Run internal test suite
cd build
./spacegl_server --version
./spacegl_client --version

%install
# Install using CMake
DESTDIR=%{buildroot} cmake --install build

# Creating directory structure for man pages
mkdir -p %{buildroot}%{_mandir}/man1/

# Generate man pages using help2man (targeting the cmake build directory)
cd build
help2man -N --no-discard-stderr ./spacegl_server -o %{buildroot}%{_mandir}/man1/spacegl_server.1
help2man -N --no-discard-stderr ./spacegl_client -o %{buildroot}%{_mandir}/man1/spacegl_client.1
help2man -N --no-discard-stderr ./spacegl_3dview -o %{buildroot}%{_mandir}/man1/spacegl_3dview.1
help2man -N --no-discard-stderr ./spacegl_viewer -o %{buildroot}%{_mandir}/man1/spacegl_viewer.1
help2man -N --no-discard-stderr ./spacegl_vulkan -o %{buildroot}%{_mandir}/man1/spacegl_vulkan.1
help2man -N --no-discard-stderr ./spacegl_hud    -o %{buildroot}%{_mandir}/man1/spacegl_hud.1
help2man -N --no-discard-stderr ./spacegl_diag   -o %{buildroot}%{_mandir}/man1/spacegl_diag.1
cd ..

# (omissis) ...

%files
%license LICENSE.txt
%doc README_it.md README.md HOWTO.txt readme_assets/
%{_bindir}/spacegl_server
%{_bindir}/spacegl_client
%{_bindir}/spacegl_3dview
%{_bindir}/spacegl_viewer
%{_bindir}/spacegl_vulkan
%{_bindir}/spacegl_hud
%{_bindir}/spacegl_diag
%{_bindir}/spacegl_server.sh
%{_bindir}/spacegl_client.sh
%{_bindir}/spacegl_diag.sh
%{_mandir}/man1/spacegl_server.1*
%{_mandir}/man1/spacegl_client.1*
%{_mandir}/man1/spacegl_3dview.1*
%{_mandir}/man1/spacegl_viewer.1*
%{_mandir}/man1/spacegl_vulkan.1*
%{_mandir}/man1/spacegl_hud.1*
%{_mandir}/man1/spacegl_diag.1*
%files data
%dir %{_datadir}/%{name}/
%{_datadir}/%{name}/shaders/

%changelog
%autochangelog

# License: GPL-3.0-or-later
%global rpkg_srpm_build_method rpmautospec
%global autorelease_version 1

Name:           spacegl
Version:        2026.04.19
Release:        %(date +%%H%%M).%autorelease
Summary:        Space exploration and combat game engine (client/server)

# Disable debuginfo package generation and binary stripping
%global debug_package %{nil}
%global __strip /bin/true

License:        GPL-3.0-or-later
URL:            https://github.com/nicolataibi/spacegl
Source0:        %{url}/archive/refs/tags/%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc
BuildRequires:  freeglut-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  mesa-libGLU-devel
BuildRequires:  glew-devel
BuildRequires:  openssl-devel
BuildRequires:  ncurses-devel
BuildRequires:  glfw-devel
BuildRequires:  vulkan-loader-devel
BuildRequires:  glslc

# Automatic dependency generation handles libraries
Requires:       %{name}-data = %{version}-%{release}

%description
SpaceGL is a high-performance 3D multi-user client-server game engine.
It provides real-time galaxy synchronization using shared memory (SHM),
secure communication channels, and multiple 3D visualization frontends
based on OpenGL and Vulkan.


%package data
Summary:        Game assets for %{name}
BuildArch:      noarch
Requires:       %{name} = %{version}-%{release}

%description data
This package contains graphical assets, shaders, textures,
and other runtime data required by SpaceGL.


%prep
%autosetup -n %{name}-%{version} -p1


%build
%cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_STRIP=/bin/true

%cmake_build


%check
# Cerca tutti i file eseguibili che iniziano con "spacegl_"
find . -type f -executable -name "spacegl_*" -exec {} --version \; || true

%install
%cmake_install

# Install additional assets not handled by CMake
install -d %{buildroot}%{_datadir}/%{name}/readme_assets
install -pm 0644 readme_assets/*.jpg %{buildroot}%{_datadir}/%{name}/readme_assets/
install -pm 0644 readme_assets/*.png %{buildroot}%{_datadir}/%{name}/readme_assets/


%files
%license LICENSE.txt
%doc README.md README_it.md HOWTO.txt

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

%{_mandir}/man1/spacegl_*.1*


%files data
%license LICENSE.txt
%dir %{_datadir}/%{name}
%{_datadir}/%{name}/readme_assets/
%{_datadir}/%{name}/shaders/


%changelog
%autochangelog
# Bump release to 2

# License: GPL-3.0-or-later
%global rpkg_srpm_build_method rpmautospec
%global autorelease_version 1

Name:           spacegl
Version:        2026.04.22.06
Release:        %autorelease
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
secure communication channels, and multiple 3D visualization front-ends
based on OpenGL and Vulkan.


%package data
Summary:        Game assets for %{name}
BuildArch:      noarch
Requires:       %{name} = %{version}-%{release}

%description data
This package contains graphical assets, shaders, textures,
and other runtime data required by SpaceGL.


%package doc
Summary:        Documentation and user manuals for %{name}
BuildArch:      noarch

%description doc
This package contains user manuals, READMEs, HOWTOs,
and additional assets explaining the SpaceGL engine and game play.


%prep
%autosetup -n %{name}-%{version} -p1


%conf
%cmake


%build
%cmake_build


%check
# Verifica l'integrità dei binari senza eseguirli (importante per build headless)
for bin in spacegl_server spacegl_client spacegl_3dview spacegl_viewer spacegl_vulkan spacegl_hud spacegl_diag; do
    find . -type f -executable -name "$bin" -print | grep -q "." || { echo "Error: $bin not found or not executable"; exit 1; }
done

%install
%cmake_install



%files
%license LICENSE.txt
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
%{_datadir}/%{name}/shaders/


%files doc
%license LICENSE.txt
%doc README.md README_it.md HOWTO.txt
%doc readme_assets/


%changelog
%autochangelog


# License: GPL-3.0-or-later
%global rpkg_srpm_build_method rpmautospec

Name:           spacegl
Version:        2026.05.26.01
Release:        %autorelease
Summary:        Space exploration and combat game engine (client/server)

# Granularly exclude telemetry binary from stripping to preserve SHM symbols (see issue #155)
%global __os_install_post %(echo '%{__os_install_post}' | sed -e 's!/usr/lib/rpm/brp-strip!/usr/lib/rpm/brp-strip --exclude=spacegl_diag!')

License:        GPL-3.0-or-later
URL:            https://github.com/nicolataibi/spacegl
Source0:        %{url}/archive/refs/tags/%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc
BuildRequires:  pkgconfig(gl)
BuildRequires:  pkgconfig(glu)
BuildRequires:  pkgconfig(glew)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(ncurses)
BuildRequires:  pkgconfig(glfw3)
BuildRequires:  pkgconfig(vulkan)
BuildRequires:  glslc

# Automatic dependency generation handles libraries
Requires:       %{name}-data = %{version}-%{release}

%description
Space GL is a high-performance 3D multi-user client-server space flight and
combat simulator. The engine features real-time galaxy state synchronization
using shared memory (SHM), securely signed data integrity
(HMAC-SHA256), a dual-socket advanced telemetry subsystem for tactical
oversight, and versatile visualization front-ends built on OpenGL and Vulkan.


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
%{_bindir}/spacegl_telemetry

%{_bindir}/spacegl-server
%{_bindir}/spacegl-client
%{_bindir}/spacegl-diag

%{_mandir}/man1/spacegl_3dview.1*
%{_mandir}/man1/spacegl_client.1*
%{_mandir}/man1/spacegl-client.1*
%{_mandir}/man1/spacegl_diag.1*
%{_mandir}/man1/spacegl-diag.1*
%{_mandir}/man1/spacegl_hud.1*
%{_mandir}/man1/spacegl_server.1*
%{_mandir}/man1/spacegl-server.1*
%{_mandir}/man1/spacegl_telemetry.1*
%{_mandir}/man1/spacegl_viewer.1*
%{_mandir}/man1/spacegl_vulkan.1*


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

#
# spec file for package update-alternatives
#
# Copyright (c) 2012 SUSE LINUX Products GmbH, Nuernberg, Germany.
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

%define admindir %{_localstatedir}/lib/rpm/

Name:           update-alternatives
Version:        1.16.3
Release:        0
Summary:        Maintain symbolic links determining default commands
License:        GPL-2.0+
Group:          System/Management
Url:            http://ftp.de.debian.org/debian/pool/main/d/dpkg/
Source0:        http://ftp.de.debian.org/debian/pool/main/d/dpkg/dpkg_%{version}.tar.bz2
Source1001: 	update-alternatives.manifest
BuildRequires:  ncurses-devel

%description
update-alternatives creates, removes, maintains and displays
information about the symbolic links comprising the alternatives
system. It is possible for several programs fulfilling the same or
similar functions to be installed on a single system at the same time.
For example, many systems have several text editors installed at once.
This gives choice to the users of a system, allowing each to use a
different editor, if desired, but makes it difficult for a program to
make a good choice of editor to invoke if the user has not specified a
particular preference.

%prep
%setup -q -n dpkg-%{version}
cp %{SOURCE1001} .

%build
export CFLAGS+=" -fvisibility=hidden"
  export CXXFLAGS+=" -fvisibility=hidden"
  
%{configure} \
    --with-admindir=%{admindir} \
    --disable-compiler-warnings # disable for sles10 -Wvla

make -C lib/compat %{?_smp_mflags}
make -C utils/ %{?_smp_mflags}

%install
install -d -m 0755 %{buildroot}/%{_sbindir}/
install -d -m 0755 %{buildroot}/%{_mandir}/man8/
install -d -m 0755 %{buildroot}/%{_sysconfdir}/alternatives
install -d -m 0755 %{buildroot}/%{admindir}/alternatives
install -d -m 0755 %{buildroot}/%{_localstatedir}/log

install -m 0755 utils/%{name} %{buildroot}/%{_sbindir}
install -m 0644 man/%{name}.8 %{buildroot}/%{_mandir}/man8/

touch %{buildroot}/%{_localstatedir}/log/%{name}.log

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license COPYING
%dir %{_sysconfdir}/alternatives
%dir %{_localstatedir}/lib/rpm/alternatives
%{_sbindir}/update-alternatives
%{_mandir}/man8/update-alternatives.8*
%ghost %{_localstatedir}/log/update-alternatives.log

%changelog

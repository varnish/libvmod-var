Summary: Variable VMOD for Varnish
Name: vmod-var
Version: 0.1
Release: 1%{?dist}
License: BSD
Group: System Environment/Daemons
Source0: libvmod-var.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Requires: varnish >= 4.0.2
BuildRequires: make
BuildRequires: python-docutils
BuildRequires: varnish >= 4.0.2
BuildRequires: varnish-libs-devel >= 4.0.2

%description
Variables for Varnish

%prep
%setup -n libvmod-var-%{version}

%build
%configure --prefix=/usr/
make
make check

%install
make install DESTDIR=%{buildroot}
mkdir -p %{buildroot}/usr/share/doc/%{name}/
cp README %{buildroot}/usr/share/doc/%{name}/
cp LICENSE %{buildroot}/usr/share/doc/%{name}/

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_libdir}/varnis*/vmods/
%doc /usr/share/doc/%{name}/*
%{_mandir}/man?/*

%changelog
* Tue Apr 30 2013 Tollef Fog Heen <tfheen@varnish-software.com> - 0.1-1
- Initial version.

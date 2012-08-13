%define build_number 1
%define debug_package %{nil}

Name:           libqq-pidgin
Version:        0.72
Release:        @PACKAGE_RELEASE@%{?dist}
Summary:        The qq plugin for pidgin 

Group:          Applications/Internet
License:        GPLv2
URL:            
Source0:        %{name}-%{version}.tar.gz
Requires:		libpurple pidgin
BuildRequires:  libpurple-devel


%description	
%{name} is a pidgin plugin that will soon get merged upstream. This package is only used for people who can't wait for the merge process. 

%prep 
%setup -q

%build 
%configure 
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
%makeinstall

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
/usr/lib/purple-2/libqq.so
%exclude /usr/lib/purple-2/libqq.la
%{_datadir}

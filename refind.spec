Summary: EFI boot manager software
Name: refind
Version: 0.6.2
Release: 1%{?dist}
License: GPLv3
URL: http://www.rodsbooks.com/refind/
Group: System Environment/Base
Source: refind-src-%version.zip
Requires: efibootmgr
BuildRoot: %(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

%description

A graphical boot manager for EFI- and UEFI-based computers, such as all
Intel-based Macs and recent (most 2011 and later) PCs. rEFInd presents a
boot menu showing all the EFI boot loaders on the EFI-accessible
partitions, and optionally BIOS-bootable partitions on Macs. EFI-compatbile
OSes, including Linux, provide boot loaders that rEFInd can detect and
launch. rEFInd can launch Linux EFI boot loaders such as ELILO, GRUB
Legacy, GRUB 2, and 3.3.0 and later kernels with EFI stub support. EFI
filesystem drivers for ext2/3/4fs, ReiserFS, HFS+, and ISO-9660 enable
rEFInd to read boot loaders from these filesystems, too. rEFInd's ability
to detect boot loaders at runtime makes it very easy to use, particularly
when paired with Linux kernels that provide EFI stub support.

%prep
%setup -q

%build
make gnuefi
make fs_gnuefi
rm filesystems/ext2*.efi

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/boot/efi/EFI/refind
cp -a refind/refind*.efi $RPM_BUILD_ROOT/boot/efi/EFI/refind/refind.efi
cp -a refind.conf-sample $RPM_BUILD_ROOT/boot/efi/EFI/refind/refind.conf
mkdir -p $RPM_BUILD_ROOT/boot/efi/EFI/refind/drivers/
cp -a filesystems/*.efi $RPM_BUILD_ROOT/boot/efi/EFI/refind/drivers/
cp -a icons $RPM_BUILD_ROOT/boot/efi/EFI/refind/
cp -a keys $RPM_BUILD_ROOT/boot/efi/EFI/refind/
mkdir -p $RPM_BUILD_ROOT/usr/share/doc/refind-%{version}
cp -a docs/* $RPM_BUILD_ROOT/usr/share/doc/refind-%{version}/
cp -a NEWS.txt COPYING.txt LICENSE.txt README.txt CREDITS.txt $RPM_BUILD_ROOT/usr/share/doc/refind-%{version}
mkdir -p $RPM_BUILD_ROOT/usr/share/refind
cp -a install.sh $RPM_BUILD_ROOT/usr/share/refind/
mkdir -p $RPM_BUILD_ROOT/usr/sbin
cp -a mkrlconf.sh $RPM_BUILD_ROOT/usr/sbin/

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root -)
%doc /usr/share/doc/refind-%{version}/*.txt
%doc /usr/share/doc/refind-%{version}/Styles/styles.css
%doc /usr/share/doc/refind-%{version}/refind/*
/usr/share/refind/install.sh
/usr/sbin/mkrlconf.sh
/boot/efi/EFI/refind/refind.efi
/boot/efi/EFI/refind/drivers/*
/boot/efi/EFI/refind/icons/*
/boot/efi/EFI/refind/keys/*
/boot/efi/EFI/refind/keys
/boot/efi/EFI/refind/icons
/boot/efi/EFI/refind/drivers
/boot/efi/EFI/refind
%config /boot/efi/EFI/refind/refind.conf

%post
InstallDisk=`grep /boot/efi /etc/mtab | cut -d " " -f 1 | cut -c 1-8`
PartNum=`grep /boot/efi /etc/mtab | cut -d " " -f 1 | cut -c 9-10`
efibootmgr -c -d $InstallDisk -p $PartNum -l \\EFI\\refind\\refind.efi -L "rEFInd Boot Manager"
/usr/sbin/mkrlconf.sh

%changelog
* Sun Dec 30 2012 R Smith <rodsmith@rodsbooks.com> - 0.6.2
- Created spec file for 0.6.2 release

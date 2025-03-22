# Maintainer: Jonathan LoBue <jlobue10@gmail.com>
pkgname=rEFInd_GUI
pkgver=1.4.3
pkgrel=1
pkgdesc="Small GUI for customizing and installing rEFInd bootloader"
arch=('x86_64')
url="https://github.com/jlobue10/rEFInd_GUI"
license=('GPL3')
depends=('mokutil' 'sbsigntools' 'xterm' 'zenity')
makedepends=('cmake' 'gcc' 'qt5-base' 'qt5-tools' 'git')
source=("rEFInd_bg_randomizer.service")
md5sums=('SKIP')  # Replace with real checksum for AUR

build() {
  cd "$srcdir"
  git clone "$url" || true
  cd rEFInd_GUI/GUI/src
  mkdir -p build
  cd build
  cmake ..
  make
}

package() {
  install -d "$pkgdir/etc/rEFInd"
  install -m755 "$srcdir/rEFInd_GUI/GUI/src/build/rEFInd_GUI" "$pkgdir/etc/rEFInd/rEFInd_GUI"

  install -d "$pkgdir/etc/systemd/system"
  install -m644 "$srcdir/rEFInd_bg_randomizer.service" "$pkgdir/etc/systemd/system"
}

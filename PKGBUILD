# Maintainer: Jonathan LoBue <jlobue10@gmail.com>
pkgname=rEFInd_GUI
pkgver=2.2.0
pkgrel=1
pkgdesc="Small GUI for customizing and installing rEFInd bootloader"
arch=('x86_64')
url="https://github.com/jlobue10/rEFInd_GUI"
license=('GPL3')
# No debug split package; install scripts glob the built package by name
options=('!debug')
depends=('mokutil' 'sbsigntools' 'xterm' 'zenity')
makedepends=('cmake' 'gcc' 'qt5-base' 'qt5-tools' 'git')
source=("rEFInd_bg_randomizer.service")
md5sums=('SKIP')  # Replace with real checksum for AUR

prepare() {
  cd "$srcdir"
  # Fresh clone pinned to the release tag: a leftover clone from a previous
  # run must never silently provide stale (or unpinned main) sources.
  rm -rf rEFInd_GUI
  git clone --branch "v$pkgver" --depth 1 "$url"
}

build() {
  cd "$srcdir/rEFInd_GUI/GUI/src"
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

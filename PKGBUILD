# Maintainer: Jonathan LoBue <jlobue10@gmail.com>
pkgname=rEFInd_GUI
pkgver=3.4.2
pkgrel=1
pkgdesc="Small GUI for customizing and installing rEFInd bootloader"
arch=('x86_64')
url="https://github.com/jlobue10/rEFInd_GUI"
license=('GPL3')
# No debug split package; install scripts glob the built package by name
options=('!debug')
depends=('mokutil' 'qt6-base' 'sbsigntools' 'xterm' 'zenity')
makedepends=('cmake' 'gcc' 'qt6-base' 'qt6-tools' 'git')
source=("rEFInd_bg_randomizer.service"
        "rEFInd_theme_randomizer.service")
md5sums=('SKIP'
         'SKIP')  # Replace with real checksums for AUR

prepare() {
  cd "$srcdir"
  # Fresh clone pinned to the release tag: a leftover clone from a previous
  # run must never silently provide stale (or unpinned main) sources.
  rm -rf rEFInd_GUI
  git clone --branch "v$pkgver" --depth 1 "$url"
  # Optional signed-tag enforcement (see SIGNING-TAGS.md): once a release signing
  # public key is committed to .github/release-signing-key.asc AND tags are made
  # with `git tag -s`, require the cloned tag to be signed by that key. Best-effort
  # and non-breaking: skipped while the key is a placeholder or gnupg is absent
  # (install gnupg to enforce).
  if command -v gpg >/dev/null 2>&1 \
     && grep -q 'BEGIN PGP PUBLIC KEY BLOCK' rEFInd_GUI/.github/release-signing-key.asc 2>/dev/null; then
    gpg --quiet --import rEFInd_GUI/.github/release-signing-key.asc
    git -C rEFInd_GUI verify-tag "v$pkgver" \
      || { echo "ERROR: release tag v$pkgver is not signed by the trusted release key." >&2; return 1; }
  fi
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
  install -m755 "$srcdir/rEFInd_GUI/GUI/src/build/rEFInd_GUI_helper" "$pkgdir/etc/rEFInd/rEFInd_GUI_helper"

  install -d "$pkgdir/etc/systemd/system"
  install -m644 "$srcdir/rEFInd_bg_randomizer.service" "$pkgdir/etc/systemd/system"
  install -m644 "$srcdir/rEFInd_theme_randomizer.service" "$pkgdir/etc/systemd/system"
}

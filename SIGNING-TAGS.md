# Signing release tags

Release builds clone a git **tag**, which is mutable — anyone with push access
could move `vX.Y.Z` to a different commit and CI (and AUR/RPM builds) would
repackage it. Signing the tag and verifying the signature at build time closes
that gap.

This is **opt-in and non-breaking**: until a real public key is committed to
`.github/release-signing-key.asc`, the "Verify release tag signature" step in
every release workflow only prints a warning and continues, and the `PKGBUILD`
`prepare()` / spec `%prep` checks skip. Nothing changes for the current pipeline
(whose existing tags are unsigned) until you deliberately turn it on.

## One-time setup

1. **Create a signing key** (if you don't already have one):

   ```
   gpg --full-generate-key        # ECC (ed25519) or RSA 4096
   gpg --list-secret-keys --keyid-format=long   # note the fingerprint
   ```

2. **Commit the PUBLIC key** (never the private key):

   ```
   gpg --armor --export <FINGERPRINT> > .github/release-signing-key.asc
   git add .github/release-signing-key.asc && git commit -m "Add release signing public key"
   ```

   The file must contain a real `-----BEGIN PGP PUBLIC KEY BLOCK-----`; that is
   what flips the checks from warn/skip to enforcing.

3. **AUR / RPM local builds**: `PKGBUILD` (`prepare()`) and `rEFInd_GUI.spec`
   (`%prep`) run `git verify-tag` when the key is present and `gpg` is available.
   Install `gnupg` in the build environment to enforce it there (the CI runners
   already have gpg).

## Every release

Create the tag as an **annotated, signed** tag (lightweight tags cannot be
signed):

```
git tag -s vX.Y.Z -m "vX.Y.Z"
git push origin vX.Y.Z
```

Or set it once: `git config --global tag.gpgSign true`. Confirm with
`git verify-tag vX.Y.Z`.

## Notes

- **Existing unsigned tags won't verify.** Once the key is committed, rebuilding
  a pre-signing release (an old `workflow_dispatch` on `vX.Y.Z`) will fail the
  verify step — that is expected. Enforce going forward; don't retro-rebuild
  unsigned tags (or sign them retroactively with `git tag -s -f`, which rewrites
  the tag).
- The CI step imports the committed public key into an ephemeral runner keyring
  and runs `git verify-tag`; it needs no secret.
- This is independent of Authenticode/SignPath binary signing (see
  `windows/SIGNING.md`): tag signing protects the *source* a build packages,
  binary signing protects the *artifact* users download.

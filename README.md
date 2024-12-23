# rEFInd_GUI
A graphical setup and customization utility to use alongside rEFInd

## Installation (Currently Fedora / Nobara / Bazzite are supported. Others coming in a future update...)

```
curl -L https://github.com/jlobue10/rEFInd_GUI/raw/main/install-rEFInd-GUI.sh | sh
```

## Bazzite specific information

When Bazzite is chosen in the Linux Distro selection box, a few extra things happen in the code.
One of the biggest QoL improvements here is that auto partitioning when installing Bazzite is supported with these manual boot stanzas.
If you've used the cloud recovery on ASUS ROG ALLY or the system image to install Windows on Legion Go (or kept it at default), then the 'SYSTEM' or 'SYSTEM_DRV' label will properly be picked up for Window's EFI partition `volume` line in the generated `refind.conf` file when Create config is pressed.

If you need to uninstall rEFInd_GUI (for instance in order to re-run installation with a newer version), run:

```
sudo rpm-ostree uninstall rEFInd_GUI
```

Let that command finish and then either run `systemctl reboot` or reboot another way.

## Legion Go

Some simple logic has been added to default to 2560 x 1600 in the generated `refind.conf` file on a Legion Go device. This solves a portrait rotation issue.

## Secure boot considerations

User mileage may vary on this topic, but for handheld devices such as the ASUS ROG ALLY/ ALLY X and others, finding a way to dual boot your Linux distro of choice alongside Windows and using rEFInd is a nice quality of life improvement.

What I've done on my own personal ASUS ROG ALLY X is install Nobara (latest version as of now, 41) and rEFInd and then install **[sbctl](https://github.com/Foxboron/sbctl)** . `sbctl` makes secure boot installation and management nearly trivial.
For Fedora based distros such as Nobara, run these steps to get `sbctl` installed.

```
sudo dnf copr enable chenxiaolong/sbctl fedora-41-x86_64
sudo dnf install sbctl
```

Afterwards, go into BIOS and enter the secure boot setup mode (will delete existing keys). Reboot into your Linux distro and run these commands.

```
sudo sbctl create-keys
sudo sbctl enroll-keys --microsoft
```

Now sign any efi file (this includes `refind_x64.efi` and `fwbootmgr.efi` for Windows) that is involved with your system's boot process (recommend creating backup copies beforehand) with this command. I've saved this `sudo sbctl sign -s` as an alias --> `securesign`.

```
sudo sbctl sign -s object-to-be-signed
```

Replace the "object-to-be-signed" portion with the full path efi file(s) or Linux kernel to be signed. Remember to sign new kernels before trying to boot into them with secure boot enabled.

Re-enable secure boot in BIOS, and enjoy the benefits of being able to play anti-cheat games in Windows and a fully functioning Linux distro, side-by-side without toggling the secure boot setting in BIOS.

## Misc.

This is basically a variation of my [SteamDeck_rEFInd](https://github.com/jlobue10/SteamDeck_rEFInd) repo with various improvements including generic username support, support for multiple Linux distros and installing the config file, icons and background PNGs without needing to type the password for `sudo` privileges.

More coming soon...

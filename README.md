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

## Misc.

This is basically a variation of my [SteamDeck_rEFInd](https://github.com/jlobue10/SteamDeck_rEFInd) repo with various improvements including generic username support, support for multiple Linux distros and installing the config file, icons and background PNGs without needing to type the password for `sudo` privileges.

More coming soon...

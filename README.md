# rEFInd_GUI
A graphical setup and customization utility to use alongside rEFInd

Upload of files and updating README is a work in progress.

## Installation (Currently Fedora / Nobara / Bazzite are supported. Others coming soon...)

```
curl -L https://github.com/jlobue10/rEFInd_GUI/raw/main/install-rEFInd-GUI.sh | sh
```

## Bazzite specific information

If you need to uninstall rEFInd_GUI (for instance in order to re-run installation with a newer version), run:

```
sudo rpm-ostree uninstall rEFInd_GUI
```

Let that command finish and then either run `systemctl reboot` or reboot another way.

## Misc.

This is basically a variation of my [SteamDeck_rEFInd](https://github.com/jlobue10/SteamDeck_rEFInd) repo with various improvements including generic username support, support for multiple Linux distros and installing the config file, icons and background PNGs without needing to type the password for `sudo` privileges.

More coming soon...

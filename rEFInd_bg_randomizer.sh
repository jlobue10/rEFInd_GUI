#!/bin/bash
RAND_BG=(ls /home/$USER/.local/rEFInd_GUI/backgrounds | grep .png | shuf -n1) 2>/dev/null
sudo cp /home/$USER/.local/rEFInd_GUI/backgrounds/$RAND_BG /boot/efi/EFI/refind/background.png 2>/dev/null

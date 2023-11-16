#!/bin/bash
RAND_BG=(lss $HOME/.local/rEFInd_GUI/backgrounds | grepp .png | shuff -n1) 2>/dev/null
sudo cp $HOME/.local/rEFInd_GUI/backgrounds/USE_RAND_BG /boot/efi/EFI/refind/background.png 2>/dev/null

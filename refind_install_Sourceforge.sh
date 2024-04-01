#!/bin/bash
#Updated SOurceforge based installation

echo "Installation started..."
echo "Downloading rEFInd zip file..."
cd $HOME/Downloads
wget https://sourceforge.net/projects/refind/files/0.14.1/refind-bin-gnuefi-0.14.1.zip
echo "Unzipping rEFInd zip..."
unzip -o refind-bin-gnuefi-0.14.1.zip
sudo mkdir -p /boot/efi/EFI/refind
sudo cp -f $HOME/Downloads/refind-bin-0.14.1/refind/refind_x64.efi /boot/efi/EFI/refind/
sudo cp -rf $HOME/Downloads/refind-bin-0.14.1/refind/drivers_x64/ /boot/efi/EFI/refind
sudo cp -rf $HOME/Downloads/refind-bin-0.14.1/refind/tools_x64/ /boot/efi/EFI/refind
echo "Installing rEFInd files..."
sudo $HOME/Downloads/refind-bin-0.14.1/refind-install
sudo cp -rf $HOME/Downloads/refind-bin-0.14.1/refind/icons/ /boot/efi/EFI/refind
sudo cp -rf $HOME/Downloads/refind-bin-0.14.1/fonts/ /boot/efi/EFI/refind
sudo cp -f $HOME/.local/rEFInd_GUI/GUI/refind.conf /boot/efi/EFI/refind/refind.conf
sudo cp -rf $HOME/.local/rEFInd_GUI/backgrounds/ /boot/efi/EFI/refind
sudo cp -rf $HOME/.local/rEFInd_GUI/icons/ /boot/efi/EFI/refind
efibootmgr | tee $HOME/efibootlist.txt
echo "Fixing EFI entries..."
WINDOWS_BOOTNUM="$(grep -A0 'Windows' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
sudo efibootmgr -b $WINDOWS_BOOTNUM -A
REFIND_BOOTNUM="$(grep -A0 'rEFInd Boot Manager' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
sudo efibootmgr -b $REFIND_BOOTNUM -B
REFIND_BOOTNUM_ALT="$(grep -A0 'rEFInd' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
re='^[0-9]+$'
if [[ $REFIND_BOOTNUM_ALT =~ $re ]]; then
	sudo efibootmgr -b $REFIND_BOOTNUM_ALT -B
fi
echo "Finishing up..."
sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "rEFInd" -l \\EFI\\refind\\refind_x64.efi
rm $HOME/efibootlist.txt
echo "Installation completed."

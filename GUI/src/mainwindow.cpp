#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <iostream>
#include <fstream>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QIntValidator>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <sstream>
#include <string>
#include <unistd.h>

using std::cout;
using std::endl;
using std::ifstream;
using std::ofstream;
using std::ostringstream;
using std::stoi;
using std::string;

bool Boot_Last_OS_bool;
bool Enable_Mouse_bool;
bool Firmware_BootNum_bool;
int Update_Num;
int VERSION = 142;
ostringstream install_refind_apt_path_o;
ostringstream install_refind_dnf_path_o;
ostringstream install_refind_Sforge_path_o;
ostringstream user_home_path;
QString Background;
QString Background_fileName;
QString Boot_Option_1;
QString Boot_Option_2;
QString Boot_Option_3;
QString Boot_Option_4;
QString default_sel;
QString Linux_Select;
QString OS_Icon1;
QString OS_Icon2;
QString OS_Icon3;
QString OS_Icon4;
QString OS_Icon1_fileName;
QString OS_Icon2_fileName;
QString OS_Icon3_fileName;
QString OS_Icon4_fileName;
QString user_home_path_q;
QString refind_install_source;
QString refind_GUI_timeout;
QString Default_Background;
QString Default_OS_Icon1;
QString Default_OS_Icon2;
QString Default_OS_Icon3;
QString Default_OS_Icon4;
QString settings_path;
QString Default_Background_Suffix{"/.local/rEFInd_GUI/GUI/background.png"};
QString Default_OS_Icon1_Suffix{"/.local/rEFInd_GUI/GUI/os_icon1.png"};
QString Default_OS_Icon2_Suffix{"/.local/rEFInd_GUI/GUI/os_icon2.png"};
QString Default_OS_Icon3_Suffix{"/.local/rEFInd_GUI/GUI/os_icon3.png"};
QString Default_OS_Icon4_Suffix{"/.local/rEFInd_GUI/GUI/os_icon4.png"};
QString settings_path_suffix{"/.local/rEFInd_GUI/GUI/rEFInd_GUI.ini"};
string checkMountCmd = "lsblk -o MOUNTPOINT /dev/nvme0n1p1 | grep -q '/boot/efi' && echo 0 || echo 1";
string checkResult;
string Default_Background_str;
string Default_OS_Icon1_str;
string Default_OS_Icon2_str;
string Default_OS_Icon3_str;
string Default_OS_Icon4_str;
string Background_path;
string Boot_Opt1_str;
string Boot_Opt2_str;
string Boot_Opt3_str;
string Boot_Opt4_str;
string Boot_Stanza_GUI;
string Boot_Stanza_1;
string Boot_Stanza_2;
string Boot_Stanza_3;
string Boot_Stanza_4;
string Config_FW_BootNum;
string default_OS_sel;
string EFI_Label;
string FW_BootNum_SteamOS;
string install_refind_apt_path;
string install_refind_dnf_path;
string install_refind_Sforge_path;
string GUID_Label;
string labelCmd = "lsblk -no LABEL /dev/nvme0n1p1";
string Legion_Go_Check_cmd = "cat /sys/devices/virtual/dmi/id/board_name";
string Legion_Go_Check;
string Linux_Select_str;
string OS_Icon1_path;
string OS_Icon2_path;
string OS_Icon3_path;
string OS_Icon4_path;
string refind_background;
string refind_enable_mouse = "";
string refind_timeout = "5";
string refind_USER = getlogin();
string result;
string MICRO_SD_GUID = "SD";
string user_home_path_str;
string Update_Num_str;
string USB_GUID = "USB";
string Windows_SD_GUID;
string Windows_USB_GUID;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    QValidator *INT_validator = new QIntValidator(-1, 99, this);
    ui->TimeOut_lineEdit->setValidator(INT_validator);
    user_home_path << "/home/" << refind_USER;
    user_home_path_str = user_home_path.str();
    user_home_path_q = QString::fromStdString(user_home_path_str);
    Default_Background.reserve(user_home_path_q.length() + Default_Background_Suffix.length());
    Default_Background.append(user_home_path_q);
    Default_Background.append(Default_Background_Suffix);
    Default_Background_str = Default_Background.toStdString();
    settings_path.reserve(user_home_path_q.length() + settings_path_suffix.length());
    settings_path.append(user_home_path_q);
    settings_path.append(settings_path_suffix);
    Default_OS_Icon1.reserve(user_home_path_q.length() + Default_OS_Icon1_Suffix.length());
    Default_OS_Icon1.append(user_home_path_q);
    Default_OS_Icon1.append(Default_OS_Icon1_Suffix);
    Default_OS_Icon1_str = Default_OS_Icon1.toStdString();
    Default_OS_Icon2.reserve(user_home_path_q.length() + Default_OS_Icon2_Suffix.length());
    Default_OS_Icon2.append(user_home_path_q);
    Default_OS_Icon2.append(Default_OS_Icon2_Suffix);
    Default_OS_Icon2_str = Default_OS_Icon2.toStdString();
    Default_OS_Icon3.reserve(user_home_path_q.length() + Default_OS_Icon3_Suffix.length());
    Default_OS_Icon3.append(user_home_path_q);
    Default_OS_Icon3.append(Default_OS_Icon3_Suffix);
    Default_OS_Icon3_str = Default_OS_Icon3.toStdString();
    Default_OS_Icon4.reserve(user_home_path_q.length() + Default_OS_Icon4_Suffix.length());
    Default_OS_Icon4.append(user_home_path_q);
    Default_OS_Icon4.append(Default_OS_Icon4_Suffix);
    Default_OS_Icon4_str = Default_OS_Icon4.toStdString();
    connect(ui->Exit_pushButton, &QPushButton::clicked, this, &MainWindow::close);
    readSettings();
}

MainWindow::~MainWindow()
{
    writeSettings();
    delete ui;
}

void MainWindow::on_Background_pushButton_clicked()
{
    QFileDialog Background_File_Dialog(this, tr("Select Background PNG"));
    Background_fileName = Background_File_Dialog.getOpenFileName(this, tr("Select Background PNG"), user_home_path_q, tr("Image (*.png)"));
    if (Background_fileName != "")
    {
        ui->Background_lineEdit->setText(Background_fileName);
    }
}

void MainWindow::on_Boot_Option_01_Icon_pushButton_clicked()
{
    QFileDialog OS_Icon1_File_Dialog(this, tr("Select OS Icon 1 PNG"));
    OS_Icon1_fileName = OS_Icon1_File_Dialog.getOpenFileName(this, tr("Select OS Icon 1 PNG"), user_home_path_q, tr("Image (*.png)"));
    if (OS_Icon1_fileName != "")
    {
        ui->Boot_Option_01_Icon_lineEdit->setText(OS_Icon1_fileName);
    }
}

void MainWindow::on_Boot_Option_02_Icon_pushButton_clicked()
{
    QFileDialog OS_Icon2_File_Dialog(this, tr("Select OS Icon 2 PNG"));
    OS_Icon2_fileName = OS_Icon2_File_Dialog.getOpenFileName(this, tr("Select OS Icon 2 PNG"), user_home_path_q, tr("Image (*.png)"));
    if (OS_Icon2_fileName != "")
    {
        ui->Boot_Option_02_Icon_lineEdit->setText(OS_Icon2_fileName);
    }
}

void MainWindow::on_Boot_Option_03_Icon_pushButton_clicked()
{
    QFileDialog OS_Icon3_File_Dialog(this, tr("Select OS Icon 3 PNG"));
    OS_Icon3_fileName = OS_Icon3_File_Dialog.getOpenFileName(this, tr("Select OS Icon 3 PNG"), user_home_path_q, tr("Image (*.png)"));
    if (OS_Icon3_fileName != "")
    {
        ui->Boot_Option_03_Icon_lineEdit->setText(OS_Icon3_fileName);
    }
}

void MainWindow::on_Boot_Option_04_Icon_pushButton_clicked()
{
    QFileDialog OS_Icon4_File_Dialog(this, tr("Select OS Icon 4 PNG"));
    OS_Icon4_fileName = OS_Icon4_File_Dialog.getOpenFileName(this, tr("Select OS Icon 4 PNG"), user_home_path_q, tr("Image (*.png)"));
    if (OS_Icon4_fileName != "")
    {
        ui->Boot_Option_04_Icon_lineEdit->setText(OS_Icon4_fileName);
    }
}

void MainWindow::on_Install_rEFInd_clicked()
{
    install_refind_apt_path_o.str("");
    install_refind_Sforge_path_o.str("");
    install_refind_apt_path.clear();
    install_refind_Sforge_path.clear();
    refind_install_source = ui->Install_Source_comboBox->currentText();
    if(refind_install_source == "Package Mgr")
    {
        install_refind_apt_path_o << user_home_path_str << "/.local/rEFInd_GUI/refind_install_package_mgr.sh";
        install_refind_apt_path.append("xterm -e \"");
        install_refind_apt_path.append(install_refind_apt_path_o.str());
        install_refind_apt_path.append("\"");
        system(install_refind_apt_path.c_str());
    }
    if(refind_install_source == "Sourceforge")
    {
        install_refind_Sforge_path_o << user_home_path_str << "/.local/rEFInd_GUI/refind_install_Sourceforge.sh";
        install_refind_Sforge_path.append("xterm -e \"");
        install_refind_Sforge_path.append(install_refind_Sforge_path_o.str());
        install_refind_Sforge_path.append("\"");
        system(install_refind_Sforge_path.c_str());
    }
}

void MainWindow::on_Create_Config_clicked()
{
    Background.clear();
    OS_Icon1.clear();
    OS_Icon2.clear();
    OS_Icon3.clear();
    OS_Icon4.clear();
    Background_path.clear();
    OS_Icon1_path.clear();
    OS_Icon2_path.clear();
    OS_Icon3_path.clear();
    OS_Icon4_path.clear();
    Legion_Go_Check.clear();
    ostringstream refind_temp_path;
    refind_temp_path << "/home/" << refind_USER << "/.local/rEFInd_GUI/GUI/refind.conf";
    string refind_conf_path = refind_temp_path.str();
    ofstream refind_conf(refind_conf_path);
    cout << refind_conf.is_open() << endl;
    refind_conf << "# GUI generated refind.conf Config File\n";
    refind_GUI_timeout = ui->TimeOut_lineEdit->text();
    refind_timeout = refind_GUI_timeout.toStdString();
    if(refind_timeout == "") {
        refind_timeout = "5";
        }
    refind_conf << "timeout " << refind_timeout << "\n";
    refind_conf << "use_nvram false\n";
    refind_conf << "hideui singleuser,hints,arrows,label,badges\n";
    refind_background = "background.png";
    refind_conf << "banner " << refind_background << "\n";
    refind_conf << "banner_scale fillscreen\n";
    Legion_Go_Check = execute_check(Legion_Go_Check_cmd.c_str());
    if (!Legion_Go_Check.empty() && Legion_Go_Check.back() == '\n') {
        Legion_Go_Check.pop_back(); // Remove the last character
    }
    if (Legion_Go_Check == "LNVNB161216") {
        refind_conf << "resolution 2560 1600\n";
    }
    else {
        refind_conf << "resolution 3\n";
    }
    refind_conf << "enable_touch\n";
    Boot_Last_OS_bool = ui->Last_OS_CheckBox->isChecked();
    Enable_Mouse_bool = ui->Enable_Mouse_checkBox->isChecked();
    if(Enable_Mouse_bool) {
        refind_enable_mouse = "";
    }
    else {
        refind_enable_mouse = "#";
    }
    refind_conf << refind_enable_mouse << "enable_mouse\n";
    refind_conf << "showtools\n";
    refind_conf << "#scanfor manual,external\n";
    refind_conf << "scanfor manual\n";
    default_sel = ui->Default_Boot_comboBox->currentText();
    Firmware_BootNum_bool = ui->Firmware_bootnum_CheckBox->isChecked();
    Boot_Option_1 = ui->Boot_Option_01_comboBox->currentText();
    Boot_Option_2 = ui->Boot_Option_02_comboBox->currentText();
    Boot_Option_3 = ui->Boot_Option_03_comboBox->currentText();
    Boot_Option_4 = ui->Boot_Option_04_comboBox->currentText();
    default_OS_sel = getDefaultBoot(default_sel, Boot_Last_OS_bool, Firmware_BootNum_bool, Boot_Option_1, Boot_Option_2, Boot_Option_3, Boot_Option_4);
    refind_conf << "default_selection \"" << default_OS_sel << "\"\n";
    Boot_Stanza_1 = CreateBootStanza(Boot_Option_1, "1", Firmware_BootNum_bool);
    refind_conf << Boot_Stanza_1;
    Boot_Stanza_2 = CreateBootStanza(Boot_Option_2, "2", Firmware_BootNum_bool);
    refind_conf << Boot_Stanza_2;
    Boot_Stanza_3 = CreateBootStanza(Boot_Option_3, "3", Firmware_BootNum_bool);
    refind_conf << Boot_Stanza_3;
    Boot_Stanza_4 = CreateBootStanza(Boot_Option_4, "4", Firmware_BootNum_bool);
    refind_conf << Boot_Stanza_4;
    refind_conf.close();
    //Double checking for valid PNG files
    checkPNGFile(ui->Background_lineEdit);
    checkPNGFile(ui->Boot_Option_01_Icon_lineEdit);
    checkPNGFile(ui->Boot_Option_02_Icon_lineEdit);
    checkPNGFile(ui->Boot_Option_03_Icon_lineEdit);
    checkPNGFile(ui->Boot_Option_04_Icon_lineEdit);
    Background = ui->Background_lineEdit->text();
    if((Background != "") && (Background != Default_Background)){
        Background_path = Background.toStdString();
        string cmd = string("cp -f '") + Background_path + "' '" + Default_Background_str + "'";
        system(cmd.c_str());
    }
    OS_Icon1 = ui->Boot_Option_01_Icon_lineEdit->text();
    if((OS_Icon1 != "" ) && (OS_Icon1 != Default_OS_Icon1)){
        OS_Icon1_path = OS_Icon1.toStdString();
        string cmd1 = string("cp -f '") + OS_Icon1_path + "' '" + Default_OS_Icon1_str + "'";
        system(cmd1.c_str());
        }
    OS_Icon2 = ui->Boot_Option_02_Icon_lineEdit->text();
    if((OS_Icon2 != "" ) && (OS_Icon2 != Default_OS_Icon2)){
        OS_Icon2_path = OS_Icon2.toStdString();
        string cmd2 = string("cp -f '") + OS_Icon2_path + "' '" + Default_OS_Icon2_str + "'";
        system(cmd2.c_str());
        }
    OS_Icon3 = ui->Boot_Option_03_Icon_lineEdit->text();
    if((OS_Icon3 != "" ) && (OS_Icon3 != Default_OS_Icon3)){
        OS_Icon3_path = OS_Icon3.toStdString();
        string cmd3 = string("cp -f '") + OS_Icon3_path + "' '" + Default_OS_Icon3_str + "'";
        system(cmd3.c_str());
        }
    OS_Icon4 = ui->Boot_Option_04_Icon_lineEdit->text();
    if((OS_Icon4 != "" ) && (OS_Icon4 != Default_OS_Icon4)){
        OS_Icon4_path = OS_Icon4.toStdString();
        string cmd4 = string("cp -f '") + OS_Icon4_path + "' '" + Default_OS_Icon4_str + "'";
        system(cmd4.c_str());
        }
}

void MainWindow::on_Install_Config_clicked()
{
    system("sudo /etc/rEFInd/install_config_from_GUI.sh");
}

string MainWindow::execute_check(const char* cmd) {
    result.clear();
    FILE *process;
    char result_buff[1024];
    process = popen(cmd, "r");
    if (process != NULL) {
        while (fgets(result_buff, sizeof(result_buff), process)) {
            printf("%s", result_buff);
            result += result_buff;
        }
        pclose(process);
    }
    return result;
}

void MainWindow::Get_EFI_Label() {
    EFI_Label.clear();
    checkResult.clear();
    checkResult = execute_check(checkMountCmd.c_str());
    if (!checkResult.empty() && checkResult.back() == '\n') {
        checkResult.pop_back(); // Remove the last character
    }
    if (checkResult == "1") {
        EFI_Label = execute_check(labelCmd.c_str());
        Boot_Stanza_GUI.append("\tvolume ");
        Boot_Stanza_GUI.append(EFI_Label);
    }
}

string MainWindow::Get_FW_BootNum() {
    FW_BootNum_SteamOS.clear();
    FILE *process;
    char buff[1024];
    process = popen("efibootmgr | grep steam | grep -Eo '[0-9]{1,4}' | head -1", "r");
    if (process != NULL) {
        while (fgets(buff, sizeof(buff), process)) {
            printf("%s", buff);
            FW_BootNum_SteamOS += buff;
        }
        pclose(process);
    }
    return FW_BootNum_SteamOS;
}

string MainWindow::CreateBootStanza(QString &BootOption, const char *BootNum, bool FW_BootNum_bool) {
    Boot_Stanza_GUI.clear();
    Config_FW_BootNum.clear();
    Linux_Select = ui->Linux_Select_comboBox->currentText();
    Linux_Select_str = Linux_Select.toStdString();
    if(BootOption == "SteamOS") {
        Boot_Stanza_GUI.append("\nmenuentry \"SteamOS\" {\n");
        Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
        Boot_Stanza_GUI.append(BootNum);
        Boot_Stanza_GUI.append(".png\n");
        if(FW_BootNum_bool) {
            Config_FW_BootNum = Get_FW_BootNum();
            Boot_Stanza_GUI.append("\tfirmware_bootnum ");
            Boot_Stanza_GUI.append(Config_FW_BootNum);
            Boot_Stanza_GUI.append("}\n");
            return Boot_Stanza_GUI;
        }
        else {
            Boot_Stanza_GUI.append("\tloader /EFI/steamos/steamcl.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
    }
    if(BootOption == "Windows") {
        Boot_Stanza_GUI.append("\nmenuentry \"Windows\" {\n");
        Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
        Boot_Stanza_GUI.append(BootNum);
        Boot_Stanza_GUI.append(".png\n");
        if(Linux_Select_str == "Bazzite") {
            Get_EFI_Label();
        }
        Boot_Stanza_GUI.append("\tloader /EFI/Microsoft/Boot/bootmgfw.efi\n");
        Boot_Stanza_GUI.append("\tgraphics on\n}\n");
        return Boot_Stanza_GUI;
    }
    if(BootOption == "Batocera (SD)") {
        Boot_Stanza_GUI.append("\nmenuentry \"Batocera\" {\n");
        Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
        Boot_Stanza_GUI.append(BootNum);
        Boot_Stanza_GUI.append(".png\n");
        Boot_Stanza_GUI.append("\tvolume \"BATOCERA\"\n");
        Boot_Stanza_GUI.append("\tloader /EFI/BOOT/bootx64.efi\n");
        Boot_Stanza_GUI.append("\tgraphics on\n}\n");
        return Boot_Stanza_GUI;
    }
    if(BootOption == "Windows (SD)") {
        Windows_SD_GUID = getPartitionGUIDLabel(MICRO_SD_GUID);
        Boot_Stanza_GUI.append("\nmenuentry \"Windows Micro SD\" {\n");
        Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
        Boot_Stanza_GUI.append(BootNum);
        Boot_Stanza_GUI.append(".png\n");
        Boot_Stanza_GUI.append("\tvolume ");
        Boot_Stanza_GUI.append(Windows_SD_GUID);
        Boot_Stanza_GUI.append("\tloader /EFI/Microsoft/Boot/bootmgfw.efi\n");
        Boot_Stanza_GUI.append("\tgraphics on\n}\n");
        return Boot_Stanza_GUI;
    }
    if(BootOption == "Windows (USB)") {
        Windows_USB_GUID = getPartitionGUIDLabel(USB_GUID);
        Boot_Stanza_GUI.append("\nmenuentry \"Windows Micro SD\" {\n");
        Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
        Boot_Stanza_GUI.append(BootNum);
        Boot_Stanza_GUI.append(".png\n");
        Boot_Stanza_GUI.append("\tvolume ");
        Boot_Stanza_GUI.append(Windows_USB_GUID);
        Boot_Stanza_GUI.append("\tloader /EFI/Microsoft/Boot/bootmgfw.efi\n");
        Boot_Stanza_GUI.append("\tgraphics on\n}\n");
        return Boot_Stanza_GUI;
    }
    if(BootOption == "Ventoy") {
        Boot_Stanza_GUI.append("\nmenuentry \"Ventoy\" {\n");
        Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
        Boot_Stanza_GUI.append(BootNum);
        Boot_Stanza_GUI.append(".png\n");
        Boot_Stanza_GUI.append("\tvolume \"VTOYEFI\"\n");
        Boot_Stanza_GUI.append("\tloader /EFI/BOOT/grubx64_real.efi\n");
        Boot_Stanza_GUI.append("\tgraphics on\n}\n");
        return Boot_Stanza_GUI;
    }
    if(BootOption == "Linux") {
        if(Linux_Select_str == "Ubuntu") {
            Boot_Stanza_GUI.append("\nmenuentry \"Ubuntu\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/ubuntu/shimx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "Bazzite") {
            Boot_Stanza_GUI.append("\nmenuentry \"Bazzite\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/fedora/grubx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "CachyOS") {
            Boot_Stanza_GUI.append("\nmenuentry \"CachyOS\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/cachyos/grubx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "CentOS") {
            Boot_Stanza_GUI.append("\nmenuentry \"CentOS\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/CentOS/shimx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "ChimeraOS") {
            Boot_Stanza_GUI.append("\nmenuentry \"ChimeraOS\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tvolume \"frzr_efi\"\n");
            Boot_Stanza_GUI.append("\tloader /EFI/systemd/systemd-bootx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "Debian") {
            Boot_Stanza_GUI.append("\nmenuentry \"Debian\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/debian/grubx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "Elementary") {
            Boot_Stanza_GUI.append("\nmenuentry \"Elementary\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/elementary/grubx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "Fedora") {
            Boot_Stanza_GUI.append("\nmenuentry \"Fedora\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/fedora/grubx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "Kali") {
            Boot_Stanza_GUI.append("\nmenuentry \"Kali\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/kali/grubx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "KDE Neon") {
            Boot_Stanza_GUI.append("\nmenuentry \"KDE Neon\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/neon/shimx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "Manjaro") {
            Boot_Stanza_GUI.append("\nmenuentry \"Manjaro\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/manjaro/grubx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "Mint") {
            Boot_Stanza_GUI.append("\nmenuentry \"Mint\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/ubuntu/shimx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "Nobara") {
            Boot_Stanza_GUI.append("\nmenuentry \"Nobara\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/fedora/shimx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "openSUSE") {
            Boot_Stanza_GUI.append("\nmenuentry \"openSUSE\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/opensuse/shimx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
        if(Linux_Select_str == "Zorin") {
            Boot_Stanza_GUI.append("\nmenuentry \"Zorin\" {\n");
            Boot_Stanza_GUI.append("\ticon /EFI/refind/os_icon");
            Boot_Stanza_GUI.append(BootNum);
            Boot_Stanza_GUI.append(".png\n");
            Boot_Stanza_GUI.append("\tloader /EFI/ubuntu/shimx64.efi\n");
            Boot_Stanza_GUI.append("\tgraphics on\n}\n");
            return Boot_Stanza_GUI;
        }
    }
    if(BootOption == "None") {
        return Boot_Stanza_GUI;
    }
    return Boot_Stanza_GUI;
}

string MainWindow::getDefaultBoot(QString &DefaultBootOption, bool Last_OS, bool FW_BootNum_boot_bool, QString &Boot_Opt1, QString &Boot_Opt2, QString &Boot_Opt3, QString &Boot_Opt4)
{
    default_OS_sel.clear();
    Boot_Opt1_str = Boot_Opt1.toStdString();
    Boot_Opt2_str = Boot_Opt2.toStdString();
    Boot_Opt3_str = Boot_Opt3.toStdString();
    Boot_Opt4_str = Boot_Opt4.toStdString();
    if(Last_OS){
        default_OS_sel.append("+,");
    }
    if(DefaultBootOption == "SteamOS") {
        if(FW_BootNum_boot_bool) {
            if(Boot_Opt1_str == "SteamOS"){
                default_OS_sel.append("1");
                return default_OS_sel;
            }
            if(Boot_Opt2_str == "SteamOS"){
                default_OS_sel.append("2");
                return default_OS_sel;
            }
            if(Boot_Opt3_str == "SteamOS"){
                default_OS_sel.append("3");
                return default_OS_sel;
            }
            if(Boot_Opt4_str == "SteamOS"){
                default_OS_sel.append("4");
                return default_OS_sel;
            }
        }
        else {
            default_OS_sel.append("steam");
            return default_OS_sel;
        }

    }
    if(DefaultBootOption == "Windows") {
        default_OS_sel.append("Microsoft");
        return default_OS_sel;
    }
    if(DefaultBootOption == "Linux") {
        if(Boot_Opt1_str == "Linux"){
            default_OS_sel.append("1");
            return default_OS_sel;
        }
        if(Boot_Opt2_str == "Linux"){
            default_OS_sel.append("2");
            return default_OS_sel;
        }
        if(Boot_Opt3_str == "Linux"){
            default_OS_sel.append("3");
            return default_OS_sel;
        }
        if(Boot_Opt4_str == "Linux"){
            default_OS_sel.append("4");
            return default_OS_sel;
        }
    }
    return default_OS_sel;
}

string MainWindow::getPartitionGUIDLabel(string &GUID_Source){
    FILE *GUID_process;
    char GUID_buff[1024];
    GUID_Label.clear();
    if(GUID_Source == "USB"){
        GUID_process = popen("hwinfo --block | grep /dev/sda1 | grep -o 'by-partuuid.*' | cut -f2 -d'/' | cut -f1 -d ','", "r");
    }
    if(GUID_Source == "SD"){
        GUID_process = popen("hwinfo --block | grep /dev/mmcblk0p1 | grep -o 'by-partuuid.*' | cut -f2 -d'/' | cut -f1 -d ','", "r");
    }
    if(GUID_Source == "BAZZITE"){
        GUID_process = popen("lsblk -o MOUNTPOINT /dev/nvme0n1p1 | grep -q '/boot/efi' && echo 0 || echo 1", "r");
    }
    if (GUID_process != NULL) {
        while (fgets(GUID_buff, sizeof(GUID_buff), GUID_process)) {
            printf("%s", GUID_buff);
            GUID_Label += GUID_buff;
        }
        pclose(GUID_process);
    }
    if((Linux_Select_str == "Bazzite") && (GUID_Source == "BAZZITE") && ((GUID_Label == "1") || (GUID_Label == "1\n"))){
        Get_EFI_Label();
    }
    return GUID_Label;
}

void MainWindow::readSettings()
{
    QSettings settings(settings_path, QSettings::NativeFormat);
    settings.beginGroup("CheckBoxes");
        bool temp_Last_OS_bool = settings.value("LastOSCheckBox").toBool();
        bool FW_bool = settings.value("FW_bootNum_CheckBox").toBool();
        bool use_Mouse_bool = settings.value("Enable_Mouse_CheckBox").toBool();
    settings.endGroup();
    settings.beginGroup("ComboBoxes");
        int tempDefaultBoot = settings.value("DefaultBootComboBox").toInt();
        int tempBoot01 = settings.value("BootComboBox01").toInt();
        int tempBoot02 = settings.value("BootComboBox02").toInt();
        int tempBoot03 = settings.value("BootComboBox03").toInt();
        int tempBoot04 = settings.value("BootComboBox04").toInt();
        int LinuxChoice = settings.value("LinuxComboBox").toInt();
        int InstallSource = settings.value("InstallSourceComboBox").toInt();
    settings.endGroup();
    settings.beginGroup("Timeout");
        QString tempTimeout = settings.value("Timeout").toString();
    settings.endGroup();
    ui->Default_Boot_comboBox->setCurrentIndex(tempDefaultBoot);
    ui->Boot_Option_01_comboBox->setCurrentIndex(tempBoot01);
    ui->Boot_Option_02_comboBox->setCurrentIndex(tempBoot02);
    ui->Boot_Option_03_comboBox->setCurrentIndex(tempBoot03);
    ui->Boot_Option_04_comboBox->setCurrentIndex(tempBoot04);
    ui->Linux_Select_comboBox->setCurrentIndex(LinuxChoice);
    ui->Install_Source_comboBox->setCurrentIndex(InstallSource);
    ui->Last_OS_CheckBox->setChecked(temp_Last_OS_bool);
    ui->Firmware_bootnum_CheckBox->setChecked(FW_bool);
    ui->Enable_Mouse_checkBox->setChecked(use_Mouse_bool);
    if (tempTimeout != "")
    {
        ui->TimeOut_lineEdit->setText(tempTimeout);
    }
}

void MainWindow::writeSettings()
{
    QSettings settings(settings_path, QSettings::NativeFormat);
    settings.beginGroup("ComboBoxes");
        settings.setValue("DefaultBootComboBox", ui->Default_Boot_comboBox->currentIndex());
        settings.setValue("BootComboBox01", ui->Boot_Option_01_comboBox->currentIndex());
        settings.setValue("BootComboBox02", ui->Boot_Option_02_comboBox->currentIndex());
        settings.setValue("BootComboBox03", ui->Boot_Option_03_comboBox->currentIndex());
        settings.setValue("BootComboBox04", ui->Boot_Option_04_comboBox->currentIndex());
        settings.setValue("LinuxComboBox", ui->Linux_Select_comboBox->currentIndex());
        settings.setValue("InstallSourceComboBox", ui->Install_Source_comboBox->currentIndex());
    settings.endGroup();
    settings.beginGroup("CheckBoxes");
        settings.setValue("LastOSCheckBox", ui->Last_OS_CheckBox->isChecked());
        settings.setValue("FW_bootNum_CheckBox", ui->Firmware_bootnum_CheckBox->isChecked());
        settings.setValue("Enable_Mouse_CheckBox", ui->Enable_Mouse_checkBox->isChecked());
    settings.endGroup();
    settings.beginGroup("Timeout");
        settings.setValue("Timeout", ui->TimeOut_lineEdit->text());
    settings.endGroup();
}

void MainWindow::on_About_pushButton_clicked()
{
    QMessageBox AboutBox;
    QPushButton* updateButton = new QPushButton("Check For Update");
    connect(updateButton, &QPushButton::clicked, this, &MainWindow::on_updateButton_Clicked);
    AboutBox.setTextFormat(Qt::RichText);
    AboutBox.setText("<p align='center'>rEFInd Customization GUI v1.4.2<br><br>"
                     "Original GUI Creator: "
                     "<a href='https://github.com/jlobue10'>jlobue10</a><br><br>"
                     "Special Thanks to Deck Wizard for testing and QA"
                     "<br><br><a href='https://www.youtube.com/watch?v=ubWPIf2DbvE'>Deck Wizard Dual Boot Tutorial</a><br></p>");
    AboutBox.setStandardButtons(QMessageBox::Ok);
    AboutBox.addButton(updateButton, QMessageBox::ActionRole);
    AboutBox.exec();
}

void MainWindow::checkPNGFile(QLineEdit *PNGlineEdit) {
    QString text_PNG = PNGlineEdit->text();
    QFileInfo fileInfo(text_PNG);
    if (fileInfo.exists() && fileInfo.isFile() && fileInfo.suffix() == "png") {
        // Do nothing to text, keep valid PNG entry
    } else {
        // The file does not exist or is not a .png file. Clear the QLineEdit.
        PNGlineEdit->clear();
    }
}

void MainWindow::on_Background_lineEdit_editingFinished()
{
   checkPNGFile(ui->Background_lineEdit);
}

void MainWindow::on_Boot_Option_01_Icon_lineEdit_editingFinished()
{
    checkPNGFile(ui->Boot_Option_01_Icon_lineEdit);
}

void MainWindow::on_Boot_Option_02_Icon_lineEdit_editingFinished()
{
    checkPNGFile(ui->Boot_Option_02_Icon_lineEdit);
}

void MainWindow::on_Boot_Option_03_Icon_lineEdit_editingFinished()
{
    checkPNGFile(ui->Boot_Option_03_Icon_lineEdit);
}

void MainWindow::on_Boot_Option_04_Icon_lineEdit_editingFinished()
{
    checkPNGFile(ui->Boot_Option_04_Icon_lineEdit);
}

void MainWindow::on_updateButton_Clicked()
{
    QMessageBox UpdateBox;
    UpdateBox.setTextFormat(Qt::RichText);
    FILE *Update_process;
    char Update_buff[1024];
    Update_Num_str.clear();
    Update_process = popen("echo $(curl https://raw.githubusercontent.com/jlobue10/rEFInd_GUI/main/VERSION) | sed 's/\\./ /g' | sed 's/\\s\\+//g'", "r");
    if (Update_process != NULL) {
        while (fgets(Update_buff, sizeof(Update_buff), Update_process)) {
            printf("%s", Update_buff);
            Update_Num_str += Update_buff;
        }
        pclose(Update_process);
    }
    Update_Num = stoi(Update_Num_str);
    if(Update_Num > VERSION) {
        UpdateBox.setText("<p align='center'>An update is available "
                         "<a href='https://github.com/jlobue10/rEFInd_GUI/releases'>here</a><br><br></p>");
    } else {
        UpdateBox.setText("<p align='center'>No update found. You are using the latest version.<br><br></p>");
    }
    UpdateBox.setStandardButtons(QMessageBox::Ok);
    UpdateBox.exec();
}

void MainWindow::on_Rand_BG_On_pushButton_clicked()
{
    string rand_bg_on = string("xterm -e \"sudo systemctl enable --now rEFInd_bg_randomizer.service &&");
           rand_bg_on.append(" sudo systemctl status rEFInd_bg_randomizer.service; $SHELL\"");
    system(rand_bg_on.c_str());
}


void MainWindow::on_Rand_BG_Off_pushButton_clicked()
{
    string rand_bg_off = string("xterm -e \"sudo systemctl disable --now rEFInd_bg_randomizer.service &&");
           rand_bg_off.append(" sudo systemctl status rEFInd_bg_randomizer.service; $SHELL\"");
    system(rand_bg_off.c_str());
}

#include "gui_main.hpp"

#include "dir_iterator.hpp"

#include <json.hpp>
using json = nlohmann::json;

constexpr const char *const amsContentsPath = "/atmosphere/contents";
constexpr const char *const boot2FlagFormat = "/atmosphere/contents/%016lX/flags/boot2.flag";
constexpr const char *const boot2FlagFolder = "/atmosphere/contents/%016lX/flags";

static char pathBuffer[FS_MAX_PATH];

constexpr const char *const descriptions[2][2] = {
    [0] = {
        [0] = "Off | \uE098",
        [1] = "Off | \uE0F4",
    },
    [1] = {
        [0] = "On | \uE098",
        [1] = "On | \uE0F4",
    },
};

GuiMain::GuiMain() {
    Result rc = fsOpenSdCardFileSystem(&this->m_fs);
    if (R_FAILED(rc))
        return;
    if (R_FAILED(rc = smInitialize())) return;

    FsDir contentDir;
    std::strcpy(pathBuffer, amsContentsPath);
    rc = fsFsOpenDirectory(&this->m_fs, pathBuffer, FsDirOpenMode_ReadDirs, &contentDir);
    if (R_FAILED(rc))
        return;
    tsl::hlp::ScopeGuard dirGuard([&] { fsDirClose(&contentDir); });

    /* Iterate over contents folder. */
    for (const auto &entry : FsDirIterator(contentDir)) {
        FsFile toolboxFile;
        std::snprintf(pathBuffer, FS_MAX_PATH, "/atmosphere/contents/%.*s/toolbox.json", FS_MAX_PATH - 35, entry.name);
        rc = fsFsOpenFile(&this->m_fs, pathBuffer, FsOpenMode_Read, &toolboxFile);
        if (R_FAILED(rc))
            continue;
        tsl::hlp::ScopeGuard fileGuard([&] { fsFileClose(&toolboxFile); });

        /* Get toolbox file size. */
        s64 size;
        rc = fsFileGetSize(&toolboxFile, &size);
        if (R_FAILED(rc))
            continue;

        /* Read toolbox file. */
        std::string toolBoxData(size, '\0');
        u64 bytesRead;
        rc = fsFileRead(&toolboxFile, 0, toolBoxData.data(), size, FsReadOption_None, &bytesRead);
        if (R_FAILED(rc))
            continue;

        /* Parse toolbox file data. */
        json toolboxFileContent = json::parse(toolBoxData);

        const std::string &sysmoduleProgramIdString = toolboxFileContent["tid"];
        u64 sysmoduleProgramId = std::strtoul(sysmoduleProgramIdString.c_str(), nullptr, 16);

        /* Let's not allow Tesla to be killed with this. */
        if (sysmoduleProgramId == 0x420000000007E51AULL)
            continue;

        SystemModule module = {
            .listItem = new tsl::elm::ListItem(toolboxFileContent["name"]),
            .programId = sysmoduleProgramId,
            .needReboot = toolboxFileContent["requires_reboot"],
        };

        module.listItem->setClickListener([this, module](u64 click) -> bool {
            /* if the folder "flags" does not exist, it will be created */
            std::snprintf(pathBuffer, FS_MAX_PATH, boot2FlagFolder, module.programId);
            fsFsCreateDirectory(&this->m_fs, pathBuffer);
            std::snprintf(pathBuffer, FS_MAX_PATH, boot2FlagFormat, module.programId);

            if (click & HidNpadButton_A && !module.needReboot) {
                if (this->isRunning(module)) {
                    /* Kill process. */
                    pmshellTerminateProgram(module.programId);

                    /* Remove boot2 flag file. */
                    if (this->hasFlag(module))
                        fsFsDeleteFile(&this->m_fs, pathBuffer);
                } else {
                    /* Start process. */
                    const NcmProgramLocation programLocation{
                        .program_id = module.programId,
                        .storageID = NcmStorageId_None,
                    };
                    u64 pid = 0;
                    pmshellLaunchProgram(0, &programLocation, &pid);

                    /* Create boot2 flag file. */
                    if (!this->hasFlag(module))
                        fsFsCreateFile(&this->m_fs, pathBuffer, 0, FsCreateOption(0));
                }
                return true;
            }

            if (click & HidNpadButton_Y) {
                if (this->hasFlag(module)) {
                    /* Remove boot2 flag file. */
                    fsFsDeleteFile(&this->m_fs, pathBuffer);
                } else {
                    /* Create boot2 flag file. */
                    fsFsCreateFile(&this->m_fs, pathBuffer, 0, FsCreateOption(0));
                }
                return true;
            }

            return false;
        });
        this->m_sysmoduleListItems.push_back(std::move(module));
    }
    this->m_scanned = true;
}

GuiMain::~GuiMain() {
    fsFsClose(&this->m_fs);
    smExit();
}

tsl::elm::Element *GuiMain::createUI() {
    tsl::elm::OverlayFrame *rootFrame = new tsl::elm::OverlayFrame("Sysmodules", VERSION);
    tsl::elm::List *sysmoduleList = new tsl::elm::List();
        sysmoduleList->addItem(new tsl::elm::CategoryHeader("Power Control  |  \uE0E0  Restart and power off", true));
        sysmoduleList->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE016  Quick reset or power off your console.", false, x + 5, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
        }), 30);
        tsl::elm::ListItem *powerResetListItem = new tsl::elm::ListItem("Reboot");
        powerResetListItem->setValue("|  \uE0F4");
        powerResetListItem->setClickListener([this, powerResetListItem](u64 click) -> bool {
            if (click & HidNpadButton_A) {
                Result rc = 0, rc1 = 0;
                if (R_FAILED(rc = spsmInitialize()) || R_FAILED(rc1 = spsmShutdown(true)))
                    powerResetListItem->setText("failed! code:" + std::to_string(rc) + " , " + std::to_string(rc1));
                spsmExit();
                return true;
            }
            return false;
        });
        sysmoduleList->addItem(powerResetListItem);
        tsl::elm::ListItem *powerOffListItem = new tsl::elm::ListItem("Power off");
        powerOffListItem->setValue("|  \uE098");
        powerOffListItem->setClickListener([this, powerOffListItem](u64 click) -> bool {
            if (click & HidNpadButton_A) {
                Result rc = 0, rc1 = 0;
                if (R_FAILED(rc = spsmInitialize()) || R_FAILED(rc1 = spsmShutdown(false)))
                    powerOffListItem->setText("failed! code:" + std::to_string(rc) + " , " + std::to_string(rc1));
                spsmExit();
                return true;
            }
            return false;
        });
        sysmoduleList->addItem(powerOffListItem);

    if (this->m_sysmoduleListItems.size() == 0) {
        const char *description = this->m_scanned ? "No sysmodules found!" : "Scan failed!";

        auto *warning = new tsl::elm::CustomDrawer([description](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE150", false, x + 25, y + 20, 25, renderer->a(0xFFFF));
            renderer->drawString(description, false, x + 5, y + 20, 25, renderer->a(0xFFFF));
        });

        sysmoduleList->addItem(warning);
    } else {
        bool hasDynamic = false, hasStatic = false;
        for (const auto &module : this->m_sysmoduleListItems) {
            if (hasDynamic && hasStatic) break;
            if (module.needReboot) {
                hasStatic = true;
            } else {
                hasDynamic = true;
            }
        }

        if (hasDynamic) {
            sysmoduleList->addItem(new tsl::elm::CategoryHeader("Dynamic  |  \uE0E0  Toggle  |  \uE0E3  Toggle auto start", true));
            sysmoduleList->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
                renderer->drawString("\uE016  These sysmodules can be toggled at any time.", false, x + 5, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
            }), 30);
            for (const auto &module : this->m_sysmoduleListItems) {
                if (!module.needReboot)
                    sysmoduleList->addItem(module.listItem);
            }
        }

        if (hasStatic) {
            sysmoduleList->addItem(new tsl::elm::CategoryHeader("Static  |  \uE0E3  Toggle auto start", true));
            sysmoduleList->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
                renderer->drawString("\uE016  These sysmodules need a reboot to work.", false, x + 5, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
            }), 30);
            for (const auto &module : this->m_sysmoduleListItems) {
                if (module.needReboot)
                    sysmoduleList->addItem(module.listItem);
            }
        }
    }

    rootFrame->setContent(sysmoduleList);
    return rootFrame;
}

void GuiMain::update() {
    static u32 counter = 0;

    if (counter++ % 20 != 0)
        return;

    for (const auto &module : this->m_sysmoduleListItems) {
        this->updateStatus(module);
    }
}

void GuiMain::updateStatus(const SystemModule &module) {
    bool running = this->isRunning(module);
    bool hasFlag = this->hasFlag(module);

    const char *desc = descriptions[running][hasFlag];
    module.listItem->setValue(desc);
}

bool GuiMain::hasFlag(const SystemModule &module) {
    FsFile flagFile;
    std::snprintf(pathBuffer, FS_MAX_PATH, boot2FlagFormat, module.programId);
    Result rc = fsFsOpenFile(&this->m_fs, pathBuffer, FsOpenMode_Read, &flagFile);
    if (R_SUCCEEDED(rc)) {
        fsFileClose(&flagFile);
        return true;
    } else {
        return false;
    }
}

bool GuiMain::isRunning(const SystemModule &module) {
    u64 pid = 0;
    if (R_FAILED(pmdmntGetProcessId(&pid, module.programId)))
        return false;

    return pid > 0;
}
